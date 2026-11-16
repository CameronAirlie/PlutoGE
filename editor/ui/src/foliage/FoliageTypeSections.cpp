#include "PlutoGE/ui/FoliageTypeSections.h"

#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/FoliageEditing.h"
#include "PlutoGE/ui/WindInspectors.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        struct SubmeshChoice
        {
            std::string label;
            std::vector<int> indices;
            bool isGroup = false;
        };

        // Submeshes named like "Oak_trunk" and "Oak_leaves" group under "Oak".
        std::string SubmeshGroupName(std::string name)
        {
            const auto trim = [](std::string token)
            {
                while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())) != 0) token.erase(token.begin());
                while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())) != 0) token.pop_back();
                return token;
            };
            name = trim(std::move(name));
            const auto delimiter = name.find_first_of("_-. ");
            if (delimiter != std::string::npos && delimiter > 0) return trim(name.substr(0, delimiter));
            while (!name.empty() && std::isdigit(static_cast<unsigned char>(name.back())) != 0) name.pop_back();
            return trim(name);
        }

        std::vector<SubmeshChoice> BuildSubmeshChoices(const render::Mesh &mesh)
        {
            std::vector<SubmeshChoice> choices;
            std::map<std::string, std::vector<int>> groups;
            for (std::size_t index = 0; index < mesh.GetSubmeshCount(); ++index)
                if (auto group = SubmeshGroupName(mesh.GetSubmesh(index).name); !group.empty())
                    groups[std::move(group)].push_back(static_cast<int>(index));
            for (const auto &[group, indices] : groups)
                if (indices.size() > 1)
                    choices.push_back({"Group: " + group + " (" + std::to_string(indices.size()) + " parts)", indices, true});
            for (std::size_t index = 0; index < mesh.GetSubmeshCount(); ++index)
            {
                const auto &name = mesh.GetSubmesh(index).name;
                choices.push_back({name.empty() ? "Part " + std::to_string(index) : name + " (" + std::to_string(index) + ")",
                                   {static_cast<int>(index)}, false});
            }
            return choices;
        }

        void SetSubmeshSelection(scene::FoliageTypeAsset &asset, std::vector<int> indices)
        {
            asset.submeshIndex = indices.size() == 1 ? indices.front() : -1;
            asset.submeshIndices = std::move(indices);
        }

        bool DrawSubmeshPicker(scene::FoliageTypeAsset &asset, const render::Mesh &mesh)
        {
            const auto choices = BuildSubmeshChoices(mesh);
            std::string preview = "Whole Mesh";
            for (const auto &choice : choices)
                if (FoliageSubmeshSelectionMatches(asset, choice.indices))
                {
                    preview = choice.label;
                    break;
                }

            bool changed = false;
            if (ImGui::BeginCombo("Mesh Part", preview.c_str()))
            {
                if (ImGui::Selectable("Whole Mesh", FoliageSubmeshSelectionMatches(asset, {})))
                {
                    SetSubmeshSelection(asset, {});
                    changed = true;
                }
                bool separated = false;
                for (const auto &choice : choices)
                {
                    if (!choice.isGroup && !separated)
                    {
                        ImGui::Separator();
                        separated = true;
                    }
                    if (ImGui::Selectable(choice.label.c_str(), FoliageSubmeshSelectionMatches(asset, choice.indices)))
                    {
                        SetSubmeshSelection(asset, choice.indices);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Which parts of the mesh each instance draws. A part keeps its offset from the mesh's pivot,\n"
                                  "so a set of several plants should be split into one model per plant instead.");
            return changed;
        }

        // What the renderer has to work with, and the way to change it: LODs and impostors are import settings.
        void DrawMeshDetail(const scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
        {
            if (!context.mesh) return;
            std::size_t lodCount = 1;
            std::size_t triangles = 0;
            for (std::size_t index = 0; index < context.mesh->GetSubmeshCount(); ++index)
            {
                lodCount = (std::max)(lodCount, context.mesh->GetSubmeshLodCount(index));
                triangles += context.mesh->GetSubmesh(index).indexCount / 3;
            }
            ImGui::TextDisabled("%zu triangles, %zu part%s, %zu LOD%s%s", triangles, context.mesh->GetSubmeshCount(),
                                context.mesh->GetSubmeshCount() == 1 ? "" : "s", lodCount, lodCount == 1 ? "" : "s",
                                context.hasImpostor ? ", impostor" : "");

            const auto &assets = core::Engine::GetInstance().GetAssetManager();
            const std::string model = asset.modelAssetId.empty() ? std::string{} : assets.ResolveStableAssetId(asset.modelAssetId);
            if (!model.empty())
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Import Settings..."))
                    EditorShell::GetInstance().RevealAsset(model);
                ImGui::SetItemTooltip("Shows the model in the Content Browser: Vegetation LODs, triangle budget and impostors are set there.");
            }
        }

        // Distant Cards: far instances drawn as the mesh's baked impostor (camera-facing cards from an atlas of views).
        // The cards are baked by the model's import, so a mesh without them gets a one-click bake here.
        bool DrawDistantCards(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
        {
            ImGui::SeparatorText("Distant Cards");
            if (context.hasImpostor)
            {
                const bool changed = ImGui::DragFloat("Cards From", &asset.impostorDistanceScale, 0.01f, 0.1f, 10.0f, "x%.2f",
                                                      ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
                ImGui::SetItemTooltip("How far away instances turn into flat impostor cards, relative to the distance the import chose.\n"
                                      "Below 1 swaps to cards sooner (cheaper forests); above 1 keeps the mesh further out.\n"
                                      "Cards light, fog and take shadows like the mesh, but do not cast shadows or sway.");
                return changed;
            }
            if (!context.mesh)
                return false;
            ImGui::TextDisabled("This mesh has no distant cards: every instance draws as a mesh at any distance.");
            const auto &assets = core::Engine::GetInstance().GetAssetManager();
            const std::string model = asset.modelAssetId.empty() ? std::string{} : assets.ResolveStableAssetId(asset.modelAssetId);
            auto &shell = EditorShell::GetInstance();
            if (model.empty() || !context.project)
            {
                ImGui::TextDisabled("Cards are baked when a model is imported: use a mesh imported from a model to get them.");
                return false;
            }
            const bool running = shell.IsModelImportRunning();
            ImGui::BeginDisabled(running || context.readOnly);
            if (ImGui::Button("Bake Distant Cards"))
            {
                assetimport::MeshImportOptions options;
                std::string error;
                if (!assetimport::ModelImportService{}.ReadOptions(*context.project, model, options, &error))
                    shell.Log(EditorShell::ConsoleSeverity::Error, "Cannot read the import settings of " + model + ": " + error);
                else
                {
                    // Cards need the hand-over a LOD chain gives; trees want the vegetation chain.
                    options.bakeImpostor = true;
                    if (!options.generateLods)
                    {
                        options.generateLods = true;
                        options.vegetationLods = true;
                    }
                    if (shell.BeginModelImport(model, options, &error))
                        shell.Log(EditorShell::ConsoleSeverity::Info, "Reimporting " + model + " with distant cards.");
                    else
                        shell.Log(EditorShell::ConsoleSeverity::Error, "Cannot reimport " + model + ": " + error);
                }
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("Turns on Bake Impostor (and Vegetation LODs if the model has no LODs) in the model's import\n"
                                  "settings and reimports it. Every scene and foliage type using the model gets the cards.");
            if (running)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", shell.GetModelImportProgress().c_str());
            }
            return false;
        }

        // The body of a section: its header always responds, its controls follow `readOnly`.
        template <typename Draw>
        bool Section(const char *label, ImGuiTreeNodeFlags flags, const FoliageTypeSectionContext &context, Draw &&draw)
        {
            if (!ImGui::CollapsingHeader(label, flags)) return false;
            ImGui::PushID(label);
            ImGui::BeginDisabled(context.readOnly);
            const bool changed = draw();
            ImGui::EndDisabled();
            ImGui::PopID();
            return changed;
        }
    }

    ImVec4 FoliageIssueColor(FoliageIssueSeverity severity)
    {
        switch (severity)
        {
        case FoliageIssueSeverity::Error: return ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
        case FoliageIssueSeverity::Warning: return ImVec4(1.0f, 0.75f, 0.3f, 1.0f);
        case FoliageIssueSeverity::Info: break;
        }
        return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    }

    void DrawFoliageIssues(const std::vector<FoliageIssue> &issues)
    {
        for (const auto &issue : issues)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, FoliageIssueColor(issue.severity));
            ImGui::TextWrapped("%s", issue.message.c_str());
            ImGui::PopStyleColor();
        }
    }

    bool FoliageSubmeshSelectionMatches(const scene::FoliageTypeAsset &asset, const std::vector<int> &indices)
    {
        if (indices.empty()) return asset.submeshIndex < 0 && asset.submeshIndices.empty();
        auto current = asset.submeshIndices;
        if (current.empty() && asset.submeshIndex >= 0) current.push_back(asset.submeshIndex);
        auto wanted = indices;
        std::sort(current.begin(), current.end());
        std::sort(wanted.begin(), wanted.end());
        return current == wanted;
    }

    bool DrawFoliageSourceSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
    {
        return Section("Source", ImGuiTreeNodeFlags_DefaultOpen, context, [&]
        {
            bool changed = false;
            static constexpr std::array meshTypes{assets::ProjectAssetType::Mesh, assets::ProjectAssetType::Model};
            auto mesh = asset.sourceMeshPath;
            if (RenderProjectAssetPicker("Mesh", context.project, std::span<const assets::ProjectAssetType>(meshTypes), mesh,
                                         {.noneLabel = nullptr, .emptyPreview = "Choose or drop a mesh...", .includeEngineAssets = true}))
            {
                SetFoliageAssetMesh(asset, mesh);
                changed = true;
            }
            if (context.mesh && context.mesh->GetSubmeshCount() > 1)
                changed |= DrawSubmeshPicker(asset, *context.mesh);
            DrawMeshDetail(asset, context);

            changed |= RenderProjectAssetPicker("Material Override", context.project, assets::ProjectAssetType::Material,
                                                asset.materialAssetReference,
                                                {.noneLabel = "Mesh's Own Materials", .includeEngineAssets = true});
            ImGui::SetItemTooltip("Draws every part with this material. Leaf transmission is set on the material.");
            if (context.drawSourceActions) context.drawSourceActions();
            return changed;
        });
    }

    bool DrawFoliagePlacementSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
    {
        return Section("Placement", ImGuiTreeNodeFlags_DefaultOpen, context, [&]
        {
            bool changed = ImGui::DragFloatRange2("Scale Range", &asset.minScale, &asset.maxScale, 0.01f, 0.01f, 50.0f, "%.2f", "%.2f");
            ImGui::SetItemTooltip("New instances take a random scale in this range. Existing instances keep theirs.");
            int orientation = asset.alignToTerrainNormal ? 1 : 0;
            if (ImGui::Combo("Growth", &orientation, "Upright (World Up)\0Terrain Normal\0"))
            {
                asset.alignToTerrainNormal = orientation == 1;
                changed = true;
            }
            ImGui::SetItemTooltip("Which way new instances grow: upright for trees, along the slope for grass and ground cover.\n"
                                  "Existing instances keep their transforms.");

            int habitat = static_cast<int>(asset.waterHabitat.mode);
            if (ImGui::Combo("Water", &habitat, "Anywhere\0Dry Land Only\0Underwater Only\0"))
            {
                asset.waterHabitat.mode = static_cast<scene::WaterHabitatMode>(habitat);
                changed = true;
            }
            ImGui::SetItemTooltip("Where the plant grows relative to lakes, rivers and the sea (still water level).\n"
                                  "The brush and procedural scatter skip positions it rejects. Existing instances stay.");
            if (asset.waterHabitat.mode == scene::WaterHabitatMode::Underwater)
            {
                if (ImGui::DragFloatRange2("Water Depth", &asset.waterHabitat.minDepth, &asset.waterHabitat.maxDepth, 0.05f, 0.0f,
                                           1000.0f, "%.2f m", "%.2f m", ImGuiSliderFlags_AlwaysClamp))
                {
                    scene::SanitizeWaterHabitat(asset.waterHabitat);
                    changed = true;
                }
                ImGui::SetItemTooltip("Metres of water above the ground where it grows: 0 - 0.6 for reeds, 1 - 20 for sea grass.");
            }
            return changed;
        });
    }

    bool DrawFoliageRenderingSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
    {
        return Section("Rendering", ImGuiTreeNodeFlags_None, context, [&]
        {
            bool changed = ImGui::DragFloat("Draw Distance", &asset.maxDrawDistance, 1.0f, 0.0f, 10000.0f,
                                            asset.maxDrawDistance > 0.0f ? "%.1f m" : "Component Default");
            ImGui::SetItemTooltip("How far instances draw. Use a short range for grass and other small foliage.\n"
                                  "0 uses the component's Max Draw Distance.");
            changed |= ImGui::DragFloat("LOD Distance Scale", &asset.lodDistanceScale, 0.01f, render::MinLodDistanceScale,
                                        render::MaxLodDistanceScale, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Multiplies the distance of this type's LOD and impostor switches: 2 keeps detail twice as far away.\n"
                                  "Triangles grow with its square. Floors such as Min Render LOD still apply.");
            changed |= DrawDistantCards(asset, context);

            int shadows = static_cast<int>(asset.shadows);
            if (ImGui::Combo("Shadows", &shadows, "Foliage Component's Setting\0Cast Shadows\0No Shadows\0"))
            {
                asset.shadows = static_cast<scene::FoliageShadowMode>(shadows);
                changed = true;
            }
            ImGui::SetItemTooltip("Whether instances cast shadows. Trees and bushes usually should; grass and small ground cover\n"
                                  "rarely need to, and skipping them saves shadow rendering.\n"
                                  "\"Foliage Component's Setting\" follows Cast Shadows on the Foliage component.");
            ImGui::BeginDisabled(asset.shadows == scene::FoliageShadowMode::Off);
            changed |= ImGui::DragFloat("Shadow Distance", &asset.maxShadowDistance, 1.0f, 0.0f, 10000.0f,
                                        asset.maxShadowDistance > 0.0f ? "%.1f m" : "Component Default");
            ImGui::SetItemTooltip("How far from the camera instances still cast shadows.\n"
                                  "0 uses the component's Max Shadow Distance.");
            ImGui::EndDisabled();

            changed |= ImGui::DragFloat("Cell Size", &asset.cellSize, 1.0f, 1.0f, 256.0f, "%.1f m");
            ImGui::SetItemTooltip("Instances are grouped into cells of this size for collision and editing, and for drawing unless\n"
                                  "Render Cluster Size is set. Small cells make local edits and physics rebuilds more granular.");
            changed |= ImGui::DragFloat("Render Cluster Size", &asset.renderClusterSize, 1.0f, 0.0f, 1024.0f,
                                        asset.renderClusterSize > 0.0f ? "%.1f m" : "Cell Size", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SetItemTooltip("Instances draw in square clusters of this size, each one draw per mesh part, culled as a whole on the CPU\n"
                                  "(GPU culling still culls each instance on screen). Larger clusters cost the CPU less per frame, but a\n"
                                  "cluster within the shadow distance casts every instance in it. 0 uses the Cell Size.");
            return changed;
        });
    }

    bool DrawFoliageCollisionSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
    {
        return Section("Collision", ImGuiTreeNodeFlags_None, context, [&]
        {
            bool changed = ImGui::Checkbox("Capsule Collision", &asset.collisionEnabled);
            ImGui::SetItemTooltip("One static capsule per instance. Fit it to the trunk; leave grass and flowers without it.");
            ImGui::BeginDisabled(!asset.collisionEnabled);
            changed |= ImGui::DragFloat3("Center", &asset.collisionCenter.x, 0.05f);
            changed |= ImGui::DragFloat("Radius", &asset.collisionRadius, 0.02f, 0.01f, 100.0f);
            changed |= ImGui::DragFloat("Height", &asset.collisionHeight, 0.05f, 0.02f, 500.0f);
            ImGui::EndDisabled();
            return changed;
        });
    }

    bool DrawFoliageWindSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
    {
        return Section("Wind", ImGuiTreeNodeFlags_None, context, [&]
        {
            return DrawFoliageWindSettings(asset.wind, context.mesh, context.sceneHasWind);
        });
    }

    bool DrawFoliageInteractionSection(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
    {
        const ImGuiTreeNodeFlags flags = asset.proxyPrefabReference.empty() ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen;
        return Section("Interaction", flags, context, [&]
        {
            const bool changed = RenderProjectAssetPicker("Proxy Prefab", context.project, assets::ProjectAssetType::Prefab,
                                                          asset.proxyPrefabReference, {.noneLabel = "None (Scenery)"});
            ImGui::SetItemTooltip("The entity an instance becomes when gameplay promotes it (an axe strikes a tree).\n"
                                  "It spawns at the instance's position, rotation and scale with its scripts running,\n"
                                  "and releasing it puts the instance back. Other instances stay cheap foliage.");
            if (!asset.proxyPrefabReference.empty())
                ImGui::TextDisabled("Scripts promote instances with RaycastHit.FoliageInstance.TryPromote.");
            return changed;
        });
    }

    bool DrawFoliageTypeSections(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
    {
        bool changed = DrawFoliageSourceSection(asset, context);
        changed |= DrawFoliagePlacementSection(asset, context);
        changed |= DrawFoliageRenderingSection(asset, context);
        changed |= DrawFoliageCollisionSection(asset, context);
        changed |= DrawFoliageWindSection(asset, context);
        changed |= DrawFoliageInteractionSection(asset, context);
        return changed;
    }
}
