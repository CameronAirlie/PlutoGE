#include "PlutoGE/ui/FoliageTypeSections.h"

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
            changed |= ImGui::DragFloat("Cell Size", &asset.cellSize, 1.0f, 1.0f, 256.0f, "%.1f m");
            ImGui::SetItemTooltip("Instances are grouped into cells of this size for culling and collision. Large cells (64-128 m)\n"
                                  "draw cheaper; small ones make local edits and physics rebuilds more granular.");
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

    bool DrawFoliageTypeSections(scene::FoliageTypeAsset &asset, const FoliageTypeSectionContext &context)
    {
        bool changed = DrawFoliageSourceSection(asset, context);
        changed |= DrawFoliagePlacementSection(asset, context);
        changed |= DrawFoliageRenderingSection(asset, context);
        changed |= DrawFoliageCollisionSection(asset, context);
        changed |= DrawFoliageWindSection(asset, context);
        return changed;
    }
}
