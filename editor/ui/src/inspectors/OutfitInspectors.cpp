#include "PlutoGE/ui/OutfitInspectors.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/CameraTagFilter.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/SkinPoseFollow.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/OutfitComponent.h"
#include "PlutoGE/scene/components/WearableComponent.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/ComponentInspectors.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/ListEditor.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

// Outfit: what is worn, top to bottom: the body it dresses, the worn list (one row per prefab: its state and fit, its
// slot, the prefab), and the skin hidden under it. Wearable: its slot, the body skin it hides (a searchable joint
// checklist from its own skeleton), and a fit check against a body the author picks. Edits go through one place that
// records undo and prefab overrides.
namespace PlutoGE::ui
{
    namespace
    {
        // ---- shared ------------------------------------------------------------------------------------------------

        // Prefab overrides for every serialized property an edit changed, added or removed.
        void EditComponent(OutfitInspectorHost &host, scene::Entity &entity, scene::Component &component,
                           const char *typeName, const char *label, const std::function<void()> &edit)
        {
            host.EditStep(label, [&]
            {
                const auto before = component.Serialize();
                edit();
                const auto after = component.Serialize();
                std::unordered_map<std::string, const std::string *> old;
                for (const auto &property : before) old.emplace(property.name, &property.value);
                const auto record = [&](const std::string &name)
                { entity.AddPrefabOverride(std::string("Component:") + typeName + ":" + name); };
                for (const auto &property : after)
                {
                    const auto found = old.find(property.name);
                    if (found == old.end() || *found->second != property.value) record(property.name);
                    if (found != old.end()) old.erase(found);
                }
                for (const auto &[name, value] : old) record(name);
            });
        }

        // An icon in a status colour, with a tooltip explaining it.
        void StatusIcon(const char *icon, EditorColor color, const std::string &tooltip)
        {
            ImGui::TextColored(GetEditorColor(color), "%s", icon);
            if (!tooltip.empty()) ImGui::SetItemTooltip("%s", tooltip.c_str());
        }

        // A muted note that wraps to the inspector's width.
        void Hint(const char *text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        int Centimetres(float metres) { return static_cast<int>(std::lround(metres * 100.0f)); }

        // What a fit report means for the author, in a sentence or two.
        std::string DescribeFit(const scene::SkinFitReport &fit)
        {
            const auto matched = std::to_string(fit.Matched()) + " of " + std::to_string(fit.joints) + " joints follow the body";
            switch (fit.quality)
            {
            case scene::SkinFitQuality::Fits:
                return "Fits this body: " + matched + (fit.matchedByName == fit.Matched() ? " by name." : ".");
            case scene::SkinFitQuality::Loose:
                return "Fits loosely: '" + fit.worstJoint + "' differs from the body by " + std::to_string(Centimetres(fit.largestProportionError)) +
                       " cm, so it may clip or float a little.\n" + matched + ".";
            case scene::SkinFitQuality::OtherBody:
                return "Made for another body: '" + fit.worstJoint + "' differs by " + std::to_string(Centimetres(fit.largestProportionError)) +
                       " cm. It will sit off this body (shoulders and head too high or low) and skin shows through.\n"
                       "Rig the garment to this body's armature in your modelling tool.\n" + matched +
                       (fit.matchedByName == 0 ? ", none by name: it was rigged to another skeleton." : ".");
            case scene::SkinFitQuality::Unmatched:
                return "Shares no joints with the body's skeleton, so it cannot follow it. Rig it to this body's armature.";
            }
            return {};
        }

        EditorColor FitColor(scene::SkinFitQuality quality)
        {
            switch (quality)
            {
            case scene::SkinFitQuality::Fits: return EditorColor::Success;
            case scene::SkinFitQuality::Loose: return EditorColor::Warning;
            default: return EditorColor::Error;
            }
        }

        const char *FitIcon(scene::SkinFitQuality quality)
        {
            return quality == scene::SkinFitQuality::Fits ? icons::Check : icons::Warning;
        }

        // The first skinned mesh on `entity` or below it.
        const scene::MeshComponent *FindSkinnedMesh(const scene::Entity &entity)
        {
            if (const auto *mesh = entity.GetComponent<scene::MeshComponent>(); mesh && mesh->GetMesh() && mesh->GetMesh()->HasSkeleton())
                return mesh;
            for (const auto *child : entity.GetChildren())
                if (child && !child->IsTransient())
                    if (const auto *mesh = FindSkinnedMesh(*child)) return mesh;
            return nullptr;
        }

        std::string MeshName(const scene::MeshComponent &mesh)
        {
            const auto &reference = mesh.GetMeshAssetReference();
            return reference.empty() ? std::string("runtime mesh") : std::filesystem::path(reference).stem().string();
        }

        // ---- outfit ------------------------------------------------------------------------------------------------

        // The list as the inspector edits it: empty rows are kept until a prefab is chosen.
        void WriteWearables(scene::OutfitComponent &outfit, const std::vector<std::string> &wearables)
        {
            std::vector<scene::Property> properties{{"Wearable Count", scene::PropertyType::Int, std::to_string(wearables.size())}};
            for (std::size_t index = 0; index < wearables.size(); ++index)
                properties.push_back({"Wearables." + std::to_string(index), scene::PropertyType::String, wearables[index]});
            outfit.Deserialize(properties);
        }

        void DrawItemStatus(const scene::OutfitComponent::WornItem *item)
        {
            using State = scene::OutfitComponent::ItemState;
            if (!item)
            {
                StatusIcon(icons::Info, EditorColor::Warning, "Not put on yet.");
                return;
            }
            switch (item->state)
            {
            case State::Worn:
                if (!item->hasSkinnedMesh)
                    StatusIcon(icons::Info, EditorColor::Warning, "Worn, but it has no skinned mesh to follow the body:\n"
                                                                  "rigid parts stay where the prefab put them.");
                else
                    StatusIcon(FitIcon(item->fit.quality), FitColor(item->fit.quality), DescribeFit(item->fit));
                break;
            case State::Covered:
                StatusIcon(icons::Info, EditorColor::Warning, item->error + "\nOne item shows per slot: the last one listed.");
                break;
            case State::Failed:
                StatusIcon(icons::Error, EditorColor::Error, item->error);
                break;
            case State::NoBody:
                StatusIcon(icons::Error, EditorColor::Error, "There is no skinned body to wear it on.");
                break;
            }
        }

        void DrawBodySummary(const scene::OutfitComponent &outfit)
        {
            const auto *body = outfit.FindBody();
            if (!body)
            {
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s No skinned body on this entity or below it.", icons::Warning);
                Hint("Add the Outfit to the character whose skinned mesh should wear the items.");
                return;
            }
            ImGui::TextDisabled("Dresses '%s' (%s)", body->GetOwner()->GetName().c_str(), MeshName(*body).c_str());
            ImGui::SetItemTooltip("Worn items are spawned under this entity and posed by its skeleton.");
        }

        void DrawCoveredSummary(const scene::OutfitComponent &outfit)
        {
            const auto *body = outfit.FindBody();
            if (!body || body->GetCoveredJoints().empty()) return;
            std::string joints;
            for (const auto &joint : body->GetCoveredJoints()) joints += (joints.empty() ? "" : ", ") + joint;
            ImGui::TextDisabled("Skin hidden under the clothes: %zu triangles", body->GetCoveredTriangleCount());
            ImGui::SetItemTooltip("Body skin carried by these joints is not drawn while the items are worn:\n%s\n"
                                  "Set by each item's Wearable component.", joints.c_str());
        }

        void InspectOutfit(scene::OutfitComponent &outfit, scene::Entity &entity, OutfitInspectorHost &host)
        {
            DrawBodySummary(outfit);
            auto wearables = outfit.GetWearables();
            const auto &items = outfit.GetWornItems();
            const auto *project = host.Project();
            const bool changed = RenderVectorEditor<std::string>("Worn", wearables, [&](std::size_t index, std::string &prefab)
            {
                const auto *item = index < items.size() && items[index].prefab == prefab ? &items[index] : nullptr;
                DrawItemStatus(item);
                ImGui::SameLine();
                const std::string slot = item && !item->slot.empty() ? item->slot : std::string(item && item->hasWearableComponent ? "Any" : "-");
                ImGui::TextDisabled("%-9s", slot.c_str());
                ImGui::SetItemTooltip("Slot, from the item's Wearable component. One item shows per slot: the last one listed.");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-FLT_MIN);
                return RenderProjectAssetPicker("##Wearable", project, assets::ProjectAssetType::Prefab, prefab,
                                                {.noneLabel = "None", .emptyPreview = "Choose a wearable prefab"});
            }, {.addLabel = "Add Wearable", .emptyLabel = "Nothing worn: add a wearable prefab.", .reorderable = true});
            if (changed)
                EditComponent(host, entity, outfit, "OutfitComponent", "Edit Outfit", [&] { WriteWearables(outfit, wearables); });
            DrawCoveredSummary(outfit);
            Hint("Worn items appear under the body, dimmed in the hierarchy. They are rebuilt from this list and not saved.");
        }

        // ---- wearable ----------------------------------------------------------------------------------------------

        void DrawSlot(scene::WearableComponent &wearable, scene::Entity &entity, OutfitInspectorHost &host)
        {
            const auto setSlot = [&](std::string slot)
            { EditComponent(host, entity, wearable, "WearableComponent", "Set Wearable Slot", [&] { wearable.SetSlot(std::move(slot)); }); };
            const auto &current = wearable.GetSlot();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
            if (ImGui::BeginCombo("Slot", current.empty() ? "None" : current.c_str()))
            {
                if (ImGui::Selectable("None", current.empty())) setSlot({});
                ImGui::SetItemTooltip("Takes no slot: worn alongside anything.");
                for (const auto suggested : scene::WearableComponent::SuggestedSlots())
                    if (ImGui::Selectable(std::string(suggested).c_str(), current == suggested)) setSlot(std::string(suggested));
                ImGui::Separator();
                static char custom[64] = {};
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
                if (ImGui::InputTextWithHint("##CustomSlot", "Other slot...", custom, sizeof(custom), ImGuiInputTextFlags_EnterReturnsTrue) && custom[0])
                {
                    setSlot(custom);
                    custom[0] = 0;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("An outfit shows one item per slot, the last one listed: another item put on in this slot covers this one.");
        }

        // Joints in skeleton order with their depth, for an indented checklist.
        std::vector<std::pair<std::size_t, int>> JointsWithDepth(const render::Skeleton &skeleton)
        {
            std::vector<std::pair<std::size_t, int>> joints;
            for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
            {
                int depth = 0;
                for (int parent = skeleton.joints[index].parentJointIndex; parent >= 0 && depth < 64;
                     parent = skeleton.joints[static_cast<std::size_t>(parent)].parentJointIndex)
                    ++depth;
                joints.emplace_back(index, depth);
            }
            return joints;
        }

        bool ContainsInsensitive(const std::string &text, const char *filter)
        {
            std::string lower(text), needle(filter);
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::ranges::transform(needle, needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower.find(needle) != std::string::npos;
        }

        void DrawCoveredJoints(scene::WearableComponent &wearable, scene::Entity &entity, OutfitInspectorHost &host)
        {
            ImGui::SeparatorText("Hides body skin");
            bool hides = wearable.GetHidesCoveredSkin();
            if (ImGui::Checkbox("Hide Covered Skin", &hides))
                EditComponent(host, entity, wearable, "WearableComponent", hides ? "Hide Covered Skin" : "Show Covered Skin",
                              [&] { wearable.SetHidesCoveredSkin(hides); });
            ImGui::SetItemTooltip("Off: the body is drawn whole under the garment. On: body skin carried by the ticked "
                                  "joints is not drawn while this is worn, so it cannot poke through.");
            if (!hides)
            {
                ImGui::TextDisabled("The body stays whole while this is worn.");
                if (!wearable.GetCoveredJoints().empty())
                    ImGui::TextDisabled("%zu joints ticked, used when this is turned on.", wearable.GetCoveredJoints().size());
                return;
            }
            Hint("Body skin carried by the ticked joints is not drawn while this is worn, so it cannot poke through. "
                 "Each joint hides only its own skin; joints below it stay.");
            const auto *mesh = FindSkinnedMesh(entity);
            auto covered = wearable.GetCoveredJoints();
            const auto commit = [&](const char *label)
            { EditComponent(host, entity, wearable, "WearableComponent", label, [&] { wearable.SetCoveredJoints(covered); }); };
            if (!mesh)
            {
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s Add the garment's skinned mesh to choose joints.", icons::Warning);
                return;
            }
            const auto &skeleton = mesh->GetMesh()->GetSkeleton();
            static std::unordered_map<scene::EntityID, std::array<char, 64>> filters;
            auto &filter = filters[entity.GetID()];
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
            ImGui::InputTextWithHint("##JointFilter", (std::string(icons::Search) + " Find joint").c_str(), filter.data(), filter.size());
            ImGui::SameLine();
            ImGui::TextDisabled("%zu ticked", covered.size());
            if (!covered.empty())
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear"))
                {
                    covered.clear();
                    commit("Clear Covered Joints");
                    return;
                }
            }
            const bool filtering = filter[0] != 0;
            if (ImGui::BeginChild("##CoveredJoints", ImVec2(-FLT_MIN, ImGui::GetTextLineHeightWithSpacing() * 10.0f), ImGuiChildFlags_Borders))
            {
                for (const auto &[index, depth] : JointsWithDepth(skeleton))
                {
                    const auto &name = skeleton.joints[index].name;
                    if (filtering && !ContainsInsensitive(name, filter.data())) continue;
                    bool ticked = std::ranges::find(covered, name) != covered.end();
                    ImGui::PushID(static_cast<int>(index));
                    if (!filtering) ImGui::Indent(static_cast<float>(depth) * ImGui::GetFontSize() * 0.6f + 0.001f);
                    if (ImGui::Checkbox(name.c_str(), &ticked))
                    {
                        if (ticked) covered.push_back(name);
                        else std::erase(covered, name);
                        commit(ticked ? "Cover Joint" : "Uncover Joint");
                    }
                    if (!filtering) ImGui::Unindent(static_cast<float>(depth) * ImGui::GetFontSize() * 0.6f + 0.001f);
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            // Names the skeleton does not have (renamed bones, another rig): they still resolve by humanoid alias on a
            // body, but are worth a look.
            for (const auto &name : wearable.GetCoveredJoints())
            {
                if (render::FindSkeletonJoint(skeleton, name) >= 0) continue;
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s '%s' is not in this skeleton", icons::Warning, name.c_str());
                ImGui::SameLine();
                ImGui::PushID(name.c_str());
                if (ImGui::SmallButton("Remove"))
                {
                    std::erase(covered, name);
                    commit("Uncover Joint");
                }
                ImGui::PopID();
            }
        }

        // An author's check of the garment against a body, before it is worn (editor state, not saved).
        void DrawFitCheck(const scene::Entity &entity, OutfitInspectorHost &host)
        {
            ImGui::SeparatorText("Fit check");
            auto &bodyReference = host.FitCheckBody(entity.GetID());
            const std::array types{assets::ProjectAssetType::Mesh, assets::ProjectAssetType::Model};
            ImGui::SetNextItemWidth(-FLT_MIN);
            RenderProjectAssetPicker("##FitBody", host.Project(), std::span<const assets::ProjectAssetType>(types), bodyReference,
                                     {.noneLabel = "None", .emptyPreview = "Choose the character's body mesh"});
            ImGui::SetItemTooltip("Compares this garment's skeleton with a body's: whether it was rigged to that body.");
            const auto *mesh = FindSkinnedMesh(entity);
            if (bodyReference.empty() || !mesh) return;
            const auto *body = host.LoadMesh(bodyReference);
            if (!body || !body->HasSkeleton())
            {
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s That mesh has no skeleton.", icons::Warning);
                return;
            }
            const auto &skeleton = mesh->GetMesh()->GetSkeleton();
            const auto fit = scene::AssessSkinFit(body->GetSkeleton(), skeleton, scene::BuildSkinPoseFollowMap(body->GetSkeleton(), skeleton));
            ImGui::TextColored(GetEditorColor(FitColor(fit.quality)), "%s", FitIcon(fit.quality));
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(DescribeFit(fit).c_str());
            ImGui::PopTextWrapPos();
        }

        void InspectWearable(scene::WearableComponent &wearable, scene::Entity &entity, OutfitInspectorHost &host)
        {
            DrawSlot(wearable, entity, host);
            if (!FindSkinnedMesh(entity))
                ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s No skinned mesh: an outfit can wear it, but nothing follows the body.",
                                   icons::Warning);
            DrawCoveredJoints(wearable, entity, host);
            DrawFitCheck(entity, host);
        }

        // ---- editor host -------------------------------------------------------------------------------------------

        class ShellHost final : public OutfitInspectorHost
        {
        public:
            ShellHost(EditorShell &shell, scene::EntityID entity) : m_shell(shell), m_entity(entity) {}
            void EditStep(const char *label, const std::function<void()> &edit) override
            {
                m_shell.ExecuteSceneEdit(label, edit, SceneEditScope::Entities({m_entity}));
            }
            const assets::Project *Project() const override { return m_shell.GetProject(); }
            const render::Mesh *LoadMesh(const std::string &reference) override
            {
                return core::Engine::GetInstance().GetAssetManager().LoadMeshAsset(reference);
            }
            std::string &FitCheckBody(std::uint32_t entity) override
            {
                static std::unordered_map<std::uint32_t, std::string> bodies;
                return bodies[entity];
            }

        private:
            EditorShell &m_shell;
            scene::EntityID m_entity;
        };

        void InspectOutfitComponent(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            properties.clear();
            ShellHost host(context.shell, context.entity.GetID());
            DrawOutfitInspector(static_cast<scene::OutfitComponent &>(component), context.entity, host);
        }

        void InspectWearableComponent(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            properties.clear();
            ShellHost host(context.shell, context.entity.GetID());
            DrawWearableInspector(static_cast<scene::WearableComponent &>(component), context.entity, host);
        }
    }

    void DrawOutfitInspector(scene::OutfitComponent &outfit, scene::Entity &entity, OutfitInspectorHost &host)
    {
        ImGui::PushID("Outfit");
        InspectOutfit(outfit, entity, host);
        ImGui::PopID();
    }

    void DrawWearableInspector(scene::WearableComponent &wearable, scene::Entity &entity, OutfitInspectorHost &host)
    {
        ImGui::PushID("Wearable");
        InspectWearable(wearable, entity, host);
        ImGui::PopID();
    }

    void RegisterOutfitInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::OutfitComponent>(), &InspectOutfitComponent);
        RegisterComponentInspector(scene::GetComponentTypeID<scene::WearableComponent>(), &InspectWearableComponent);
    }
}
