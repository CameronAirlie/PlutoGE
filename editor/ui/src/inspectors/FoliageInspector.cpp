#include "PlutoGE/ui/ComponentInspectors.h"

#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/SceneWind.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/FoliageDiagnostics.h"
#include "PlutoGE/ui/FoliageEditing.h"
#include "PlutoGE/ui/FoliageEditTool.h"
#include "PlutoGE/ui/FoliageTypeSections.h"
#include "PlutoGE/ui/FoliageViewportTool.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

// The foliage inspector, top to bottom: the tool, the types, the selected type's settings, its placed instances, and
// the component's own settings. Each part is a function of the state it needs (docs/FOLIAGE.md).
namespace PlutoGE::ui
{
    namespace
    {
        // A button whose action waits for a second click in a small popup; for what discards painted work.
        bool ConfirmedButton(const char *label, const std::string &question)
        {
            ImGui::PushID(label);
            if (ImGui::Button(label)) ImGui::OpenPopup("Confirm");
            bool confirmed = false;
            if (ImGui::BeginPopup("Confirm"))
            {
                ImGui::TextUnformatted(question.c_str());
                if (ImGui::Button("Yes"))
                {
                    confirmed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::PopID();
            return confirmed;
        }

        std::string InstanceCountText(std::size_t count)
        {
            return std::to_string(count) + (count == 1 ? " instance" : " instances");
        }

        void DrawToolRow(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            auto &tool = context.shell.GetFoliageTool();
            const auto entityId = context.entity.GetID();
            ImGui::BeginDisabled(context.shell.GetEngine().IsRuntimeRunning());
            DrawFoliageToolModeButtons(tool, entityId);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%zu instances in total", foliage.GetTotalInstanceCount());

            if (tool.IsActiveFor(entityId) && tool.GetMode() != FoliageToolMode::Select)
            {
                const auto *terrain = context.entity.GetComponent<scene::TerrainComponent>();
                DrawFoliageIssues(DiagnoseFoliagePainting(terrain && terrain->IsEnabled(), foliage.GetSelectedType()));
            }
        }

        void DrawTypeRow(ComponentInspectorContext &context, scene::FoliageComponent &foliage, std::size_t index)
        {
            const auto *type = foliage.GetType(index);
            constexpr float kRowHeight = 30.0f;
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable("##Row", static_cast<int>(index) == foliage.GetSelectedTypeIndex(), ImGuiSelectableFlags_None, ImVec2(0.0f, kRowHeight)))
                foliage.SetSelectedTypeIndex(static_cast<int>(index));
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            const bool rowVisible = ImGui::IsItemVisible();
            ImGui::PopID();
            if (!rowVisible) return;

            auto *drawList = ImGui::GetWindowDrawList();
            const float textHeight = ImGui::GetTextLineHeight();
            const float textY = min.y + (kRowHeight - textHeight) * 0.5f;
            const auto text = ImGui::GetColorU32(ImGuiCol_Text);
            const auto dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);

            const float iconSize = kRowHeight - 4.0f;
            if (const auto thumbnail = type->asset.sourceMeshPath.empty() ? 0 : context.shell.GetAssetThumbnail(type->asset.sourceMeshPath))
                drawList->AddImage(static_cast<ImTextureID>(thumbnail), ImVec2(min.x + 2.0f, min.y + 2.0f),
                                   ImVec2(min.x + 2.0f + iconSize, min.y + 2.0f + iconSize), ImVec2(0, 1), ImVec2(1, 0));
            else
                drawList->AddText(ImVec2(min.x + 8.0f, textY), dim, icons::EntityFoliage);

            const auto count = std::to_string(type->instances.size());
            const float countWidth = ImGui::CalcTextSize(count.c_str()).x;
            float right = max.x - countWidth - 6.0f;
            drawList->AddText(ImVec2(right, textY), dim, count.c_str());

            const auto issues = DiagnoseFoliageType(*type);
            const bool flagged = !issues.empty() && WorstFoliageIssue(issues) != FoliageIssueSeverity::Info;
            if (flagged)
            {
                right -= 16.0f;
                drawList->AddText(ImVec2(right, textY), ImGui::ColorConvertFloat4ToU32(FoliageIssueColor(WorstFoliageIssue(issues))), "!");
            }

            const float nameX = min.x + iconSize + 8.0f;
            drawList->PushClipRect(ImVec2(nameX, min.y), ImVec2(right - 4.0f, max.y), true);
            drawList->AddText(ImVec2(nameX, textY), text, type->name.c_str());
            drawList->PopClipRect();

            if (ImGui::IsItemHovered() && !issues.empty())
                ImGui::SetTooltip("%s", issues.front().message.c_str());
        }

        void DrawTypeList(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            static std::array<char, 64> filter{};
            if (foliage.GetTypeCount() > 6)
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##FoliageTypeFilter", "Filter types...", filter.data(), filter.size());
            }
            const std::string needle = foliage.GetTypeCount() > 6 ? filter.data() : "";
            const auto matches = [&](const std::string &name)
            {
                if (needle.empty()) return true;
                return std::search(name.begin(), name.end(), needle.begin(), needle.end(), [](char a, char b)
                    { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) != name.end();
            };

            const float rowHeight = 30.0f + ImGui::GetStyle().ItemSpacing.y;
            const float listHeight = rowHeight * static_cast<float>(std::clamp<std::size_t>(foliage.GetTypeCount(), 1, 7)) + ImGui::GetStyle().WindowPadding.y * 2.0f;
            if (ImGui::BeginChild("##FoliageTypes", ImVec2(0.0f, listHeight), ImGuiChildFlags_Borders))
            {
                for (std::size_t index = 0; index < foliage.GetTypeCount(); ++index)
                    if (matches(foliage.GetType(index)->name))
                        DrawTypeRow(context, foliage, index);
            }
            ImGui::EndChild();

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload);
                    payload && payload->Data && payload->DataSize > 0)
                {
                    const auto *data = static_cast<const char *>(payload->Data);
                    AddFoliageTypeFromAsset(context.shell, context.entity, foliage, std::string(data, data + payload->DataSize - 1));
                }
                ImGui::EndDragDropTarget();
            }

            if (ImGui::Button("Add Type"))
            {
                context.shell.ExecuteSceneEdit("Add Foliage Type", [&]
                {
                    foliage.AddType();
                    context.entity.AddPrefabOverride("Component:FoliageComponent:FoliageTypeCount");
                    context.entity.AddPrefabOverride("Component:FoliageComponent:SelectedType");
                });
            }
            ImGui::SetItemTooltip("Adds an empty type. Dropping a mesh, model or foliage type on the list creates one from it.");
            ImGui::SameLine();
            ImGui::BeginDisabled(foliage.GetTypeCount() <= 1);
            const auto index = static_cast<std::size_t>((std::max)(0, foliage.GetSelectedTypeIndex()));
            const auto *selected = foliage.GetType(index);
            if (ConfirmedButton("Remove Type", "Remove \"" + (selected ? selected->name : std::string{}) + "\" and its " +
                                                   InstanceCountText(selected ? selected->instances.size() : 0) + "?"))
                RemoveFoliageType(context.shell, context.entity, foliage, index);
            ImGui::EndDisabled();
        }

        void DrawAssetLink(ComponentInspectorContext &context, scene::FoliageComponent &foliage, std::size_t typeIndex)
        {
            auto &shell = context.shell;
            const auto *type = foliage.GetType(typeIndex);
            if (!type->asset.assetReference.empty())
            {
                const auto &reference = type->asset.assetReference;
                ImGui::TextDisabled("Linked to %s", std::filesystem::path(reference).filename().string().c_str());
                ImGui::SetItemTooltip("%s\nThese settings come from the asset, shared by every type linked to it.", reference.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Edit Asset")) shell.OpenFoliageTypeAsset(reference);
                ImGui::SetItemTooltip("Opens the asset in the Foliage Editor, with a live preview on this type.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Make Local")) MakeFoliageTypeLocal(shell, context.entity, foliage, typeIndex);
                ImGui::SetItemTooltip("Keeps the current settings in this scene and drops the link.");
                return;
            }

            if (ImGui::SmallButton("Save as Asset"))
            {
                const auto created = CreateFoliageTypeAssetFile(shell, type->name, type->asset);
                if (!created.empty() && LinkFoliageType(shell, context.entity, foliage, typeIndex, created))
                {
                    shell.RevealAsset(created);
                    shell.Log(EditorShell::ConsoleSeverity::Info, "Saved foliage type: " + created);
                }
            }
            ImGui::SetItemTooltip("Writes these settings to a reusable .plutofoliage asset and links this type to it.");
            ImGui::SameLine();
            std::string linkTarget;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (RenderProjectAssetPicker("##LinkFoliageAsset", shell.GetProject(), assets::ProjectAssetType::FoliageType, linkTarget,
                                         {.noneLabel = nullptr, .emptyPreview = "Link to existing asset..."}))
                LinkFoliageType(shell, context.entity, foliage, typeIndex, linkTarget);
        }

        void DrawSelectedType(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            auto *type = foliage.GetSelectedType();
            if (!type) return;
            const auto typeIndex = static_cast<std::size_t>(foliage.GetSelectedTypeIndex());

            ImGui::Spacing();
            ImGui::SeparatorText("Selected Type");
            std::array<char, 128> name{};
            std::snprintf(name.data(), name.size(), "%s", type->name.c_str());
            if (ImGui::InputText("Name", name.data(), name.size()))
            {
                foliage.SetTypeName(typeIndex, name.data());
                RecordFoliageTypeOverride(context.entity, typeIndex, "Name");
                context.shell.MarkSceneDirty();
            }
            DrawAssetLink(context, foliage, typeIndex);
            DrawFoliageIssues(DiagnoseFoliageType(*foliage.GetType(typeIndex)));
            type = foliage.GetType(typeIndex);

            FoliageTypeSectionContext sections;
            sections.project = context.shell.GetProject();
            sections.mesh = type->mesh;
            sections.hasImpostor = type->impostor != nullptr;
            sections.sceneHasWind = !render::BuildSceneWind(context.entity.GetScene()).Calm();
            sections.readOnly = !type->asset.assetReference.empty();
            sections.drawSourceActions = [&]
            {
                const auto &source = foliage.GetType(typeIndex)->asset.sourceMeshPath;
                ImGui::BeginDisabled(source.empty() || assets::Project::IsEngineAssetReference(source));
                if (ImGui::SmallButton("Generate LODs"))
                    GenerateFoliageTypeLods(context.shell, context.entity, foliage, typeIndex);
                ImGui::EndDisabled();
                ImGui::SetItemTooltip("Builds LODs for this mesh now. For control over them, use the model's import settings.");
            };

            auto edited = type->asset;
            if (DrawFoliageTypeSections(edited, sections))
                ApplyFoliageTypeEdit(context.shell, context.entity, foliage, typeIndex, edited);
        }

        // Applies the same scale edit to placed instances; see docs/FOLIAGE.md "Rescaling existing instances".
        void ApplyScaleEdit(EditorShell &shell, const scene::FoliageScaleEdit &edit, const std::vector<scene::Entity *> &targets,
                            std::optional<std::size_t> typeIndex, const char *scope)
        {
            std::size_t changedCount = 0;
            for (auto *target : targets)
            {
                auto *component = target->GetComponent<scene::FoliageComponent>();
                RunFoliageInstanceEdit(shell, *target, *component, "Scale Foliage", [&] { changedCount += component->ApplyInstanceScaleEdit(edit, typeIndex); });
            }
            if (changedCount == 0)
                shell.Log(EditorShell::ConsoleSeverity::Info, "Foliage scales already match; nothing changed.");
            else
                shell.Log(EditorShell::ConsoleSeverity::Info, "Rescaled " + std::to_string(changedCount) + " foliage instances (" + scope + ").");
        }

        void CollectFoliageEntities(scene::Entity *entity, std::vector<scene::Entity *> &entities)
        {
            if (!entity) return;
            if (entity->GetComponent<scene::FoliageComponent>()) entities.push_back(entity);
            for (auto *child : entity->GetChildren()) CollectFoliageEntities(child, entities);
        }

        void DrawScaleTools(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            if (!ImGui::TreeNode("Rescale Instances")) return;
            auto &shell = context.shell;
            auto &edit = shell.GetFoliageTool().scaleEdit;

            int mode = static_cast<int>(edit.mode);
            if (ImGui::Combo("Mode##FoliageRescale", &mode, "Multiply\0Set\0Randomize In Range\0"))
                edit.mode = static_cast<scene::FoliageScaleMode>(mode);
            switch (edit.mode)
            {
            case scene::FoliageScaleMode::Multiply:
                ImGui::DragFloat("Factor##FoliageRescale", &edit.factor, 0.01f, 0.01f, 100.0f, "%.3f");
                ImGui::SetItemTooltip("Multiplies current scales, keeping each instance's variation and proportions.");
                break;
            case scene::FoliageScaleMode::Set:
                ImGui::DragFloat("Scale##FoliageRescale", &edit.scale, 0.01f, 0.01f, 100.0f, "%.3f");
                ImGui::SetItemTooltip("Gives every instance this same uniform scale.");
                break;
            case scene::FoliageScaleMode::Randomize:
                ImGui::DragFloatRange2("Range##FoliageRescale", &edit.minScale, &edit.maxScale, 0.01f, 0.01f, 100.0f, "%.3f", "%.3f");
                ImGui::SetItemTooltip("Gives each instance a uniform scale in this range, the same one each time.");
                if (const auto *type = foliage.GetSelectedType(); type && ImGui::SmallButton("Use Type's Range"))
                {
                    edit.minScale = type->asset.minScale;
                    edit.maxScale = type->asset.maxScale;
                }
                break;
            }

            ImGui::TextDisabled("Apply to");
            ImGui::SameLine();
            ImGui::BeginDisabled(foliage.GetSelectedTypeInstanceCount() == 0);
            if (ImGui::SmallButton("Selected Type"))
                ApplyScaleEdit(shell, edit, {&context.entity}, static_cast<std::size_t>(foliage.GetSelectedTypeIndex()), "selected type");
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(foliage.GetTotalInstanceCount() == 0);
            if (ImGui::SmallButton("All Types"))
                ApplyScaleEdit(shell, edit, {&context.entity}, std::nullopt, "all types on this entity");
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Whole Scene"))
            {
                std::vector<scene::Entity *> targets;
                if (auto *scene = shell.GetScene())
                    for (auto *root : scene->GetRootEntities()) CollectFoliageEntities(root, targets);
                ApplyScaleEdit(shell, edit, targets, std::nullopt, "whole scene");
            }
            ImGui::SetItemTooltip("Every foliage type on every entity in the scene.");
            ImGui::TreePop();
        }

        void DrawBulkTools(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            auto &shell = context.shell;
            auto &entity = context.entity;
            DrawScaleTools(context, foliage);

            auto *terrain = entity.GetComponent<scene::TerrainComponent>();
            const auto sampleHeight = [terrain](float x, float z) { return terrain->GetHeightAtLocalPosition(x, z); };
            ImGui::BeginDisabled(!terrain || foliage.GetSelectedTypeInstanceCount() == 0);
            if (ImGui::Button("Snap Type To Terrain"))
                RunFoliageInstanceEdit(shell, entity, foliage, "Snap Foliage To Terrain", [&] { foliage.SnapSelectedTypeInstancesToSurface(sampleHeight); });
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!terrain || foliage.GetTotalInstanceCount() == 0);
            if (ImGui::Button("Snap All To Terrain"))
                RunFoliageInstanceEdit(shell, entity, foliage, "Snap Foliage To Terrain", [&] { foliage.SnapAllInstancesToSurface(sampleHeight); });
            ImGui::EndDisabled();
            if (!terrain) ImGui::SetItemTooltip("Needs a Terrain component on this entity.");

            // Kept on their own line, away from the everyday tools: these discard painted work.
            ImGui::BeginDisabled(foliage.GetSelectedTypeInstanceCount() == 0);
            if (ConfirmedButton("Clear Type", "Remove " + InstanceCountText(foliage.GetSelectedTypeInstanceCount()) + " of the selected type?"))
                RunFoliageInstanceEdit(shell, entity, foliage, "Clear Foliage Type", [&] { foliage.ClearSelectedTypeInstances(); });
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(foliage.GetTotalInstanceCount() == 0);
            if (ConfirmedButton("Clear All Types", "Remove all " + InstanceCountText(foliage.GetTotalInstanceCount()) + " on this entity?"))
                RunFoliageInstanceEdit(shell, entity, foliage, "Clear All Foliage", [&] { foliage.ClearInstances(); });
            ImGui::EndDisabled();
        }

        // The list scrolls over one row per instance, drawing only the ones on screen, so any count stays responsive.
        void DrawInstanceList(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            auto &tool = context.shell.GetFoliageTool();
            const auto *type = foliage.GetSelectedType();
            if (!type || type->instances.empty())
            {
                ImGui::TextDisabled("This type has no placed instances.");
                return;
            }

            const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
            const float height = rowHeight * static_cast<float>(std::clamp<std::size_t>(type->instances.size(), 1, 8)) + ImGui::GetStyle().WindowPadding.y * 2.0f;
            if (ImGui::BeginChild("##FoliageInstances", ImVec2(0.0f, height), ImGuiChildFlags_Borders))
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(type->instances.size()));
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        const auto &instance = type->instances[static_cast<std::size_t>(row)];
                        std::array<char, 96> label{};
                        std::snprintf(label.data(), label.size(), "#%llu   %.1f, %.1f, %.1f##%d",
                                      static_cast<unsigned long long>(instance.id), instance.position.x,
                                      instance.position.y, instance.position.z, row);
                        if (ImGui::Selectable(label.data(), tool.selection.Contains(instance.id)))
                        {
                            const auto &io = ImGui::GetIO();
                            if (io.KeyCtrl || io.KeySuper) tool.selection.Toggle(instance.id);
                            else if (io.KeyShift) tool.selection.Add(instance.id);
                            else tool.selection.Set(instance.id);
                            // The gizmo lives in Select mode.
                            tool.SetMode(FoliageToolMode::Select, context.entity.GetID());
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            context.shell.FrameSelectedEntity();
                    }
                }
            }
            ImGui::EndChild();
        }

        void DrawSelectedInstances(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            auto &shell = context.shell;
            auto &tool = shell.GetFoliageTool();
            if (tool.GetTarget() != context.entity.GetID()) return;
            const auto locations = tool.selection.Resolve(foliage);
            if (locations.empty())
            {
                ImGui::TextDisabled("Select instances here, or with Select in the viewport.");
                return;
            }

            const auto *primary = foliage.GetInstance(locations.back());
            const auto current = *primary;
            if (locations.size() == 1)
                ImGui::Text("Instance #%llu", static_cast<unsigned long long>(current.id));
            else
                ImGui::Text("%zu instances selected; editing moves them all", locations.size());

            auto edited = current;
            const auto field = [&](const char *label, glm::vec3 &value, float speed, float low = 0.0f, float high = 0.0f)
            {
                const bool changed = ImGui::DragFloat3(label, &value.x, speed, low, high);
                if (ImGui::IsItemActivated()) BeginFoliageTransform(shell, tool, foliage);
                const bool finished = ImGui::IsItemDeactivated();
                return std::pair{changed, finished};
            };
            const auto position = field("Position", edited.position, 0.05f);
            const auto rotation = field("Rotation", edited.rotationDegrees, 0.25f);
            const auto scale = field("Scale", edited.scale, 0.01f, 0.0001f, 100.0f);

            if ((position.first || rotation.first || scale.first) && !tool.transformBefore.empty())
            {
                // Relative to where the gesture started, so a drag's rounding never accumulates across a group.
                const auto before = std::find_if(tool.transformBefore.begin(), tool.transformBefore.end(),
                                                 [&](const scene::FoliageInstance &instance) { return instance.id == current.id; });
                if (before != tool.transformBefore.end())
                    UpdateFoliageTransform(tool, foliage, ApplyFoliagePrimaryEdit(tool.transformBefore, *before, edited));
            }
            if (position.second || rotation.second || scale.second)
                CommitFoliageTransform(shell, tool, context.entity, foliage, "Edit Foliage Instances");

            if (ImGui::Button("Frame")) shell.FrameSelectedEntity();
            ImGui::SetItemTooltip("Moves the view to the selection (F).");
            ImGui::SameLine();
            auto *terrain = context.entity.GetComponent<scene::TerrainComponent>();
            ImGui::BeginDisabled(!terrain);
            if (ImGui::Button("Snap To Terrain"))
                SnapSelectedFoliageInstances(shell, tool, context.entity, foliage,
                                             [terrain](float x, float z) { return terrain->GetHeightAtLocalPosition(x, z); });
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Delete")) DeleteSelectedFoliageInstances(shell, tool, context.entity, foliage);
            ImGui::SetItemTooltip("Removes the selected instances (Delete).");
            ImGui::SameLine();
            if (ImGui::Button("Deselect")) tool.selection.Clear();
        }

        void DrawInstances(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            const std::string header = "Instances (" + std::to_string(foliage.GetSelectedTypeInstanceCount()) + ")###FoliageInstancesHeader";
            if (!ImGui::CollapsingHeader(header.c_str())) return;
            DrawInstanceList(context, foliage);
            DrawSelectedInstances(context, foliage);
            ImGui::SeparatorText("All Instances");
            DrawBulkTools(context, foliage);
        }

        void DrawComponentSettings(ComponentInspectorContext &context, scene::FoliageComponent &foliage)
        {
            if (!ImGui::CollapsingHeader("Component Settings")) return;
            const auto changed = [&](const char *property)
            {
                context.entity.AddPrefabOverride(std::string("Component:FoliageComponent:") + property);
                context.shell.MarkSceneDirty();
            };

            bool castShadows = foliage.GetCastShadows();
            if (ImGui::Checkbox("Cast Shadows", &castShadows))
            {
                foliage.SetCastShadows(castShadows);
                changed("CastShadows");
            }
            ImGui::SetItemTooltip("Whether this component's instances cast shadows. Dense small plants are usually cheaper without.");
            bool isStatic = foliage.IsStatic();
            if (ImGui::Checkbox("Static", &isStatic))
            {
                foliage.SetStatic(isStatic);
                changed("Static");
            }
            ImGui::SetItemTooltip("Include in baked lighting and treat as unmoving.");

            float drawDistance = foliage.GetMaxDrawDistance();
            if (ImGui::DragFloat("Max Draw Distance", &drawDistance, 1.0f, 0.0f, 100000.0f, "%.1f m"))
            {
                foliage.SetMaxDrawDistance(drawDistance);
                changed("MaxDrawDistance");
            }
            ImGui::SetItemTooltip("How far instances draw, for types without their own Draw Distance. 0 means no limit.");
            float shadowDistance = foliage.GetMaxShadowDistance();
            if (ImGui::DragFloat("Max Shadow Distance", &shadowDistance, 1.0f, 0.0f, 100000.0f, "%.1f m"))
            {
                foliage.SetMaxShadowDistance(shadowDistance);
                changed("MaxShadowDistance");
            }
            ImGui::SetItemTooltip("How far instances cast shadows. 0 means no limit.");

            int minRenderLod = foliage.GetMinRenderLod();
            if (ImGui::SliderInt("Finest Render LOD", &minRenderLod, 0, 8))
            {
                foliage.SetMinRenderLod(minRenderLod);
                changed("MinRenderLod");
            }
            ImGui::SetItemTooltip("Never draw finer than this LOD. 0 uses every LOD; higher saves triangles but stays coarse up close.");
            int minShadowLod = foliage.GetMinShadowLod();
            if (ImGui::SliderInt("Finest Shadow LOD", &minShadowLod, 0, 8))
            {
                foliage.SetMinShadowLod(minShadowLod);
                changed("MinShadowLod");
            }
            ImGui::SetItemTooltip("Never cast shadows from finer than this LOD. Higher saves shadow triangles.");
        }

        void InspectFoliage(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            auto &foliage = static_cast<scene::FoliageComponent &>(component);
            properties.clear(); // Every setting has a place below; instance arrays are not for a text field.
            DrawToolRow(context, foliage);
            DrawTypeList(context, foliage);
            DrawSelectedType(context, foliage);
            DrawInstances(context, foliage);
            DrawComponentSettings(context, foliage);
        }
    }

    void RegisterFoliageInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::FoliageComponent>(), &InspectFoliage);
    }
}
