#include "PlutoGE/ui/FoliageTypeUsage.h"

#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui.h>

#include <cfloat>

namespace PlutoGE::ui
{
    namespace
    {
        void CollectUsers(scene::Entity &entity, const std::string &reference, std::vector<FoliageTypeUser> &users)
        {
            if (const auto *foliage = entity.GetComponent<scene::FoliageComponent>())
                for (std::size_t index = 0; index < foliage->GetTypeCount(); ++index)
                {
                    const auto *type = foliage->GetType(index);
                    if (type->asset.assetReference == reference)
                        users.push_back({entity.GetID(), entity.GetName(), index, type->name, type->instances.size()});
                }
            for (auto *child : entity.GetChildren())
                CollectUsers(*child, reference, users);
        }
    }

    std::vector<FoliageTypeUser> FindFoliageTypeUsers(scene::Scene &scene, const std::string &reference)
    {
        std::vector<FoliageTypeUser> users;
        if (reference.empty()) return users;
        for (auto *root : scene.GetRootEntities())
            CollectUsers(*root, reference, users);
        return users;
    }

    const scene::FoliageType *FindFoliageTypeLinkedTo(scene::Scene &scene, const std::string &reference)
    {
        const auto users = FindFoliageTypeUsers(scene, reference);
        if (users.empty()) return nullptr;
        auto *entity = scene.FindEntityByID(users.front().entity);
        auto *foliage = entity ? entity->GetComponent<scene::FoliageComponent>() : nullptr;
        return foliage ? foliage->GetType(users.front().typeIndex) : nullptr;
    }

    FoliageUsageViewResult DrawFoliageUsageView(const std::vector<FoliageTypeUser> &users, const FoliageUsageViewOptions &options)
    {
        FoliageUsageViewResult result;
        ImGui::SeparatorText("Used In This Scene");
        if (users.empty())
            ImGui::TextDisabled("No foliage type in this scene uses it.");

        const float paintWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        for (std::size_t index = 0; index < users.size(); ++index)
        {
            const auto &user = users[index];
            ImGui::PushID(static_cast<int>(index));
            const std::string label = user.entityName + ": " + user.typeName + " (" + std::to_string(user.instanceCount) + ")";
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x - paintWidth, ImGui::GetFrameHeight())))
                result.show = index;
            ImGui::SetItemTooltip("%zu placed. Click to show this type in the Inspector.", user.instanceCount);
            ImGui::SameLine();
            ImGui::BeginDisabled(!options.editable);
            if (widgets::IconButton("##Paint", icons::Paint, "Select this type and start painting it"))
                result.paint = index;
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        ImGui::Spacing();
        const std::string addLabel = options.addTargetName.empty() ? std::string("Add to Selected Entity")
                                                                   : "Add to " + options.addTargetName;
        ImGui::BeginDisabled(!options.editable || options.addTargetName.empty());
        if (ImGui::Button(addLabel.c_str(), ImVec2(-FLT_MIN, 0.0f)))
            result.addToTarget = true;
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(options.addTargetName.empty() ? "Select an entity with a Foliage component to add this type to it."
                                                            : "Adds a type linked to this asset to the selected entity.");
        return result;
    }
}
