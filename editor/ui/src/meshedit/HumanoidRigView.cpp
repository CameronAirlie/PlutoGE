#include "PlutoGE/ui/meshedit/MeshEditorViews.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/meshedit/HumanoidRigEditing.h"
#include "PlutoGE/ui/meshedit/MeshAssetSession.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <functional>
#include <numeric>
#include <optional>
#include <utility>

namespace PlutoGE::ui::meshedit
{
    namespace
    {
        using Bone = render::HumanoidBone;

        constexpr float kJointPickRadius = 8.0f; // pixels
        constexpr float kFitMargin = 1.12f;

        // Left side cool, right side warm, so either view reads at a glance.
        ImVec4 PartColor(HumanoidBodyPart part)
        {
            switch (part)
            {
            case HumanoidBodyPart::LeftArm: return ImVec4(0.33f, 0.68f, 1.00f, 1.0f);
            case HumanoidBodyPart::LeftLeg: return ImVec4(0.30f, 0.84f, 0.78f, 1.0f);
            case HumanoidBodyPart::RightArm: return ImVec4(1.00f, 0.47f, 0.45f, 1.0f);
            case HumanoidBodyPart::RightLeg: return ImVec4(1.00f, 0.68f, 0.36f, 1.0f);
            case HumanoidBodyPart::Body:
            default: return ImVec4(0.80f, 0.68f, 1.00f, 1.0f);
            }
        }

        ImVec4 BoneColor(Bone bone) { return PartColor(GetHumanoidBodyPart(bone)); }

        ImU32 U32(ImVec4 color, float alpha = 1.0f)
        {
            color.w *= alpha;
            return ImGui::ColorConvertFloat4ToU32(color);
        }

        bool ContainsInsensitive(std::string_view text, std::string_view search)
        {
            return std::search(text.begin(), text.end(), search.begin(), search.end(), [](char a, char b)
                               { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) != text.end();
        }

        const char *JointName(const render::Skeleton &skeleton, int joint)
        {
            return render::IsValidSkeletonJoint(skeleton, joint) ? skeleton.joints[static_cast<std::size_t>(joint)].name.c_str() : "";
        }

        bool IsFollower(const HumanoidRigAnalysisCache &rig, int joint)
        {
            return std::ranges::find(rig.status.hipsFollowers, joint) != rig.status.hipsFollowers.end();
        }

        void RefreshDerived(const MeshAssetSession &session, HumanoidRigViewState &state)
        {
            if (state.diagramRevision == session.GetGeometryRevision() && state.diagramView == state.view)
                return;
            const bool geometryChanged = state.diagramRevision != session.GetGeometryRevision();
            state.diagram = BuildRigDiagram(session.GetMesh(), state.view);
            state.diagramRevision = session.GetGeometryRevision();
            state.diagramView = state.view;
            state.zoom = 1.0f;
            state.pan = glm::vec2(0.0f);
            if (!geometryChanged)
                return;

            // Joints in hierarchy order, for the joint picker.
            const auto &joints = session.GetSkeleton().joints;
            std::vector<std::vector<int>> children(joints.size());
            std::vector<int> roots;
            for (std::size_t index = 0; index < joints.size(); ++index)
            {
                const int parent = joints[index].parentJointIndex;
                if (render::IsValidSkeletonJoint(session.GetSkeleton(), parent) && parent != static_cast<int>(index))
                    children[static_cast<std::size_t>(parent)].push_back(static_cast<int>(index));
                else
                    roots.push_back(static_cast<int>(index));
            }
            state.jointTree.clear();
            std::vector<std::uint8_t> visited(joints.size(), 0);
            std::vector<std::pair<int, int>> stack;
            for (auto it = roots.rbegin(); it != roots.rend(); ++it)
                stack.emplace_back(*it, 0);
            while (!stack.empty())
            {
                const auto [joint, depth] = stack.back();
                stack.pop_back();
                if (visited[static_cast<std::size_t>(joint)])
                    continue;
                visited[static_cast<std::size_t>(joint)] = 1;
                state.jointTree.emplace_back(joint, depth);
                const auto &list = children[static_cast<std::size_t>(joint)];
                for (auto it = list.rbegin(); it != list.rend(); ++it)
                    stack.emplace_back(*it, depth + 1);
            }
            // Joints in a parent cycle are not reachable from a root; list them anyway.
            for (std::size_t index = 0; index < joints.size(); ++index)
                if (!visited[index])
                    state.jointTree.emplace_back(static_cast<int>(index), 0);
        }

        // Assigns `joint` (-1 clears) to `bone`'s slot and, with `advance`, selects the next empty slot.
        void AssignJoint(MeshAssetSession &session, HumanoidRigViewState &state, Bone bone, int joint, bool advance)
        {
            if (bone == Bone::Count)
                return;
            Bone moved = Bone::Count;
            session.ModifyRig([&](render::Skeleton &skeleton) { moved = AssignHumanoidJoint(skeleton, bone, joint); });
            state.notice.clear();
            if (moved != Bone::Count)
                state.notice = std::string("Moved ") + JointName(session.GetSkeleton(), joint) + " from " + render::HumanoidBoneName(moved) +
                               " to " + render::HumanoidBoneName(bone) + ".";
            if (joint >= 0 && advance)
                if (const Bone next = NextUnassignedHumanoidBone(session.GetSkeleton(), bone); next != Bone::Count)
                    state.selected = next;
        }

        void AssignToSelected(MeshAssetSession &session, HumanoidRigViewState &state, int joint)
        {
            AssignJoint(session, state, state.selected, joint, state.advanceAfterAssign);
        }

        // A click on a joint: assigns it to the selected slot, or with no slot selected, selects the joint's slot.
        void ClickJoint(MeshAssetSession &session, HumanoidRigViewState &state, int joint)
        {
            if (state.selected == Bone::Count || session.IsReadOnly())
            {
                const Bone bone = FindHumanoidBoneForJoint(session.GetSkeleton(), joint);
                state.notice = bone == Bone::Count && !session.IsReadOnly()
                                   ? std::string("Choose a bone in the list, then click ") + JointName(session.GetSkeleton(), joint) + " to assign it."
                                   : std::string{};
                if (bone != Bone::Count)
                    state.selected = bone;
                return;
            }
            AssignToSelected(session, state, joint);
        }

        // ---------------------------------------------------------------- toolbar

        void DrawRigToolbar(MeshAssetSession &session, HumanoidRigViewState &state, const HumanoidRigAnalysisCache &rig)
        {
            const auto &status = rig.status;
            const bool readOnly = session.IsReadOnly();
            if (status.IsReady())
                widgets::Pill((std::string(icons::Check) + "  Rig ready").c_str(), GetEditorColor(EditorColor::Success));
            else if (status.requiredMapped < status.requiredCount)
                widgets::Pill((std::to_string(status.requiredCount - status.requiredMapped) + " required bones unassigned").c_str(),
                              GetEditorColor(EditorColor::Warning));
            else
                widgets::Pill((std::string(icons::Warning) + "  " + std::to_string(status.problems.size()) +
                               (status.problems.size() == 1 ? " problem" : " problems")).c_str(),
                              GetEditorColor(EditorColor::Warning));
            ImGui::SameLine();
            ImGui::BeginDisabled(readOnly);
            if (ImGui::Button("Auto Map"))
            {
                session.ModifyRig(render::AutoMapHumanoidRig);
                state.notice.clear();
            }
            ImGui::SetItemTooltip("Fill empty slots from joint names (Mixamo, Unreal, Blender and common names) and the hierarchy.\n"
                                  "Slots that already have a joint keep it.");
            ImGui::SameLine();
            if (ImGui::Button("Mirror Sides"))
            {
                int filled = 0;
                session.ModifyRig([&](render::Skeleton &skeleton) { filled = MirrorHumanoidRig(skeleton); });
                state.notice = filled == 0 ? "No empty slot had a mirrored joint name on the other side."
                                           : "Filled " + std::to_string(filled) + (filled == 1 ? " slot" : " slots") + " from the other side.";
            }
            ImGui::SetItemTooltip("Fill empty slots on one side from the other side's joints with the side swapped in the name\n"
                                  "(hand.L to hand.R, LeftArm to RightArm, Bip01 L Thigh to Bip01 R Thigh).");
            ImGui::SameLine();
            if (widgets::IconButton("##RigMore", icons::Ellipsis, "More"))
                ImGui::OpenPopup("##RigMoreMenu");
            if (ImGui::BeginPopup("##RigMoreMenu"))
            {
                if (ImGui::MenuItem("Clear All Joints"))
                    session.ModifyRig([](render::Skeleton &skeleton)
                                      {
                                          for (auto &mapping : skeleton.humanoidBoneMappings)
                                              mapping.targetJointIndex = -1;
                                      });
                if (ImGui::MenuItem("Remove Humanoid Map"))
                    session.ModifyRig([](render::Skeleton &skeleton) { skeleton.humanoidBoneMappings.clear(); });
                ImGui::SetItemTooltip("Clips then bind to joints by name or by index, without retargeting.");
                ImGui::Separator();
                ImGui::MenuItem("Select Next Empty Slot After Assigning", nullptr, &state.advanceAfterAssign);
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();

            // View controls at the right edge.
            const ImGuiStyle &style = ImGui::GetStyle();
            const char *namesLabel = "All names";
            const float segmentWidth = ImGui::GetFontSize() * 8.5f;
            const float width = segmentWidth + style.ItemSpacing.x * 3.0f + ImGui::GetFrameHeight() * 2.4f +
                                ImGui::CalcTextSize(namesLabel).x + ImGui::GetFrameHeight();
            ImGui::SameLine((std::max)(ImGui::GetCursorPosX() + style.ItemSpacing.x, ImGui::GetContentRegionMax().x - width));
            constexpr std::array<const char *, 2> views = {"Front", "Side"};
            int view = static_cast<int>(state.view);
            if (widgets::SegmentedControl("##RigViewMode", &view, views, segmentWidth))
                state.view = static_cast<RigView>(view);
            ImGui::SameLine();
            widgets::ToggleSwitch("##AllNames", &state.showAllJointNames);
            ImGui::SetItemTooltip("Label every joint, not only the mapped ones.");
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(namesLabel);
            ImGui::SameLine();
            if (widgets::IconButton("##FrameRig", icons::Frame, "Frame the whole rig (F)"))
            {
                state.zoom = 1.0f;
                state.pan = glm::vec2(0.0f);
            }
        }

        // ---------------------------------------------------------------- slot list

        void DrawSlotList(const MeshAssetSession &session, HumanoidRigViewState &state, const HumanoidRigAnalysisCache &rig)
        {
            const auto &skeleton = session.GetSkeleton();
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const float rowHeight = ImGui::GetFrameHeight();
            for (const auto &part : GetHumanoidBodyParts())
            {
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                ImGui::TextUnformatted(part.label);
                ImGui::PopStyleColor();
                for (const Bone bone : part.bones)
                {
                    ImGui::PushID(static_cast<int>(bone));
                    const int joint = render::GetHumanoidTargetJoint(skeleton, bone);
                    const auto problems = rig.ProblemsFor(bone);
                    const bool required = render::IsRequiredHumanoidBone(bone);
                    const ImVec2 min = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##Slot", state.selected == bone, ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight)))
                    {
                        state.selected = bone;
                        state.notice.clear();
                    }
                    const bool hovered = ImGui::IsItemHovered();
                    if (hovered && joint >= 0)
                        state.highlightedJoint = joint;
                    const ImVec2 max(min.x + ImGui::GetItemRectSize().x, min.y + rowHeight);

                    // Status: filled when assigned, a hollow ring when empty, warm when it needs attention.
                    const ImVec2 dot(min.x + 9.0f, (min.y + max.y) * 0.5f);
                    const ImVec4 color = BoneColor(bone);
                    if (!problems.empty())
                    {
                        const ImVec2 size = ImGui::CalcTextSize(icons::Warning);
                        drawList->AddText(ImVec2(dot.x - size.x * 0.5f, dot.y - size.y * 0.5f), GetEditorColorU32(EditorColor::Warning), icons::Warning);
                    }
                    else if (joint >= 0)
                        drawList->AddCircleFilled(dot, 4.5f, U32(color));
                    else
                        drawList->AddCircle(dot, 4.0f, required ? GetEditorColorU32(EditorColor::Warning) : GetEditorColorU32(EditorColor::TextMuted, 0.7f), 0, 1.5f);

                    const float textY = (min.y + max.y - ImGui::GetTextLineHeight()) * 0.5f;
                    const float nameX = min.x + 22.0f;
                    drawList->AddText(ImVec2(nameX, textY), GetEditorColorU32(EditorColor::Text), render::HumanoidBoneName(bone));
                    const float nameEnd = nameX + ImGui::CalcTextSize(render::HumanoidBoneName(bone)).x;
                    if (required && joint < 0)
                        drawList->AddText(ImVec2(nameEnd + 2.0f, textY), GetEditorColorU32(EditorColor::Warning), "*");

                    // The joint, right-aligned and clipped to the space left.
                    const char *value = joint >= 0 ? JointName(skeleton, joint) : "-";
                    const float available = max.x - nameEnd - 18.0f;
                    if (available > 12.0f)
                    {
                        const float valueWidth = ImGui::CalcTextSize(value).x;
                        const float x = (std::max)(nameEnd + 12.0f, max.x - 6.0f - valueWidth);
                        drawList->PushClipRect(ImVec2(nameEnd + 12.0f, min.y), ImVec2(max.x - 6.0f, max.y), true);
                        drawList->AddText(ImVec2(x, textY), GetEditorColorU32(EditorColor::TextMuted), value);
                        drawList->PopClipRect();
                    }

                    if (hovered)
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("%s%s", render::HumanoidBoneName(bone), required ? " (required)" : " (optional)");
                        ImGui::TextDisabled("%s", joint >= 0 ? JointName(skeleton, joint) : "Unassigned");
                        for (const auto *problem : problems)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
                            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                            ImGui::TextWrapped("%s", problem->message.c_str());
                            ImGui::PopTextWrapPos();
                            ImGui::PopStyleColor();
                        }
                        ImGui::EndTooltip();
                    }
                    ImGui::PopID();
                }
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::TextDisabled("* required");
        }

        // ---------------------------------------------------------------- canvas

        struct CanvasTransform
        {
            ImVec2 centre;      // screen point of the diagram centre
            glm::vec2 focus;    // diagram point at the centre
            float scale = 1.0f; // pixels per diagram unit

            [[nodiscard]] ImVec2 ToScreen(glm::vec2 point) const
            {
                return ImVec2(centre.x + (point.x - focus.x) * scale, centre.y - (point.y - focus.y) * scale);
            }
            [[nodiscard]] glm::vec2 ToDiagram(ImVec2 point) const
            {
                return glm::vec2(focus.x + (point.x - centre.x) / scale, focus.y - (point.y - centre.y) / scale);
            }
        };

        CanvasTransform MakeTransform(const RigDiagram &diagram, const HumanoidRigViewState &state, ImVec2 min, ImVec2 size)
        {
            const glm::vec2 extent = glm::max(diagram.Size(), glm::vec2(1e-3f));
            const float fit = (std::min)(size.x / (extent.x * kFitMargin), size.y / (extent.y * kFitMargin));
            CanvasTransform transform;
            transform.centre = ImVec2(min.x + size.x * 0.5f, min.y + size.y * 0.5f);
            transform.focus = (diagram.min + diagram.max) * 0.5f + state.pan;
            transform.scale = (std::max)(1e-3f, fit * state.zoom);
            return transform;
        }

        void DrawSilhouette(ImDrawList *drawList, const RigDiagram &diagram, const CanvasTransform &transform)
        {
            const auto &silhouette = diagram.silhouette;
            if (silhouette.rows == 0)
                return;
            const float cell = diagram.Size().y / static_cast<float>(silhouette.rows);
            const ImU32 fill = GetEditorColorU32(EditorColor::Text, 0.085f);
            // One rectangle per run of covered cells keeps the draw list small.
            for (int row = 0; row < silhouette.rows; ++row)
            {
                for (int column = 0; column < silhouette.columns;)
                {
                    if (!silhouette.IsCovered(column, row))
                    {
                        ++column;
                        continue;
                    }
                    const int start = column;
                    while (column < silhouette.columns && silhouette.IsCovered(column, row))
                        ++column;
                    const glm::vec2 topLeft(diagram.min.x + static_cast<float>(start) * cell, diagram.max.y - static_cast<float>(row) * cell);
                    const glm::vec2 bottomRight(diagram.min.x + static_cast<float>(column) * cell, diagram.max.y - static_cast<float>(row + 1) * cell);
                    drawList->AddRectFilled(transform.ToScreen(topLeft), transform.ToScreen(bottomRight), fill);
                }
            }
        }

        struct Label
        {
            int joint = -1;
            int priority = 0; // lower draws first and claims space first
            std::string slot;
            std::string name;
            ImVec4 color;
        };

        void DrawLabels(ImDrawList *drawList, const std::vector<ImVec2> &points, std::vector<Label> labels, ImVec2 canvasMin, ImVec2 canvasMax)
        {
            std::ranges::stable_sort(labels, {}, &Label::priority);
            std::vector<ImVec4> placed; // min.xy, max.xy
            const ImVec2 padding(5.0f, 2.0f);
            const float centreX = (canvasMin.x + canvasMax.x) * 0.5f;
            const float lineHeight = ImGui::GetTextLineHeight();
            for (const auto &label : labels)
            {
                const ImVec2 anchor = points[static_cast<std::size_t>(label.joint)];
                const float slotWidth = label.slot.empty() ? 0.0f : ImGui::CalcTextSize(label.slot.c_str()).x + 6.0f;
                const ImVec2 size(slotWidth + ImGui::CalcTextSize(label.name.c_str()).x + padding.x * 2.0f, lineHeight + padding.y * 2.0f);
                const float side = anchor.x >= centreX ? 1.0f : -1.0f;
                const float gap = 14.0f;
                const std::array<ImVec2, 8> offsets = {{
                    {side, 0.0f}, {side, -1.0f}, {side, 1.0f}, {-side, 0.0f}, {side, -2.0f}, {side, 2.0f}, {-side, -1.0f}, {-side, 1.0f},
                }};
                const bool important = label.priority <= 1;
                std::optional<ImVec2> chosen;
                for (const auto &offset : offsets)
                {
                    const float x = offset.x > 0.0f ? anchor.x + gap : anchor.x - gap - size.x;
                    const float y = anchor.y - size.y * 0.5f + offset.y * (size.y + 2.0f);
                    const ImVec4 rect(x, y, x + size.x, y + size.y);
                    if (rect.x < canvasMin.x || rect.z > canvasMax.x || rect.y < canvasMin.y || rect.w > canvasMax.y)
                        continue;
                    const bool overlaps = std::ranges::any_of(placed, [&](const ImVec4 &other)
                                                              { return rect.x < other.z + 2.0f && rect.z + 2.0f > other.x && rect.y < other.w + 1.0f && rect.w + 1.0f > other.y; });
                    if (!overlaps)
                    {
                        chosen = ImVec2(x, y);
                        break;
                    }
                }
                if (!chosen)
                {
                    if (!important)
                        continue;
                    chosen = ImVec2(side > 0.0f ? anchor.x + gap : anchor.x - gap - size.x, anchor.y - size.y * 0.5f);
                }
                const ImVec2 min = *chosen;
                const ImVec2 max(min.x + size.x, min.y + size.y);
                placed.emplace_back(min.x, min.y, max.x, max.y);
                const ImVec2 edge(min.x > anchor.x ? min.x : max.x, (min.y + max.y) * 0.5f);
                drawList->AddLine(anchor, edge, U32(label.color, 0.55f), 1.0f);
                drawList->AddRectFilled(min, max, GetEditorColorU32(EditorColor::Panel, 0.92f), 4.0f);
                drawList->AddRect(min, max, U32(label.color, label.slot.empty() ? 0.25f : 0.6f), 4.0f);
                float x = min.x + padding.x;
                if (!label.slot.empty())
                {
                    drawList->AddText(ImVec2(x, min.y + padding.y), U32(label.color), label.slot.c_str());
                    x += slotWidth;
                }
                drawList->AddText(ImVec2(x, min.y + padding.y), GetEditorColorU32(label.slot.empty() ? EditorColor::TextMuted : EditorColor::Text), label.name.c_str());
            }
        }

        void DrawJointMenus(MeshAssetSession &session, HumanoidRigViewState &state)
        {
            const auto &skeleton = session.GetSkeleton();
            if (ImGui::BeginPopup("##ChooseJoint"))
            {
                ImGui::TextDisabled("These joints share this spot:");
                for (const int joint : state.pickChoices)
                {
                    ImGui::PushID(joint);
                    const Bone bone = FindHumanoidBoneForJoint(skeleton, joint);
                    if (ImGui::Selectable(JointName(skeleton, joint)))
                        ClickJoint(session, state, joint);
                    if (ImGui::IsItemHovered())
                        state.highlightedJoint = joint;
                    if (bone != Bone::Count)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(BoneColor(bone), "%s", render::HumanoidBoneName(bone));
                    }
                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopup("##JointMenu"))
            {
                const int joint = state.contextJoint;
                const Bone mappedTo = FindHumanoidBoneForJoint(skeleton, joint);
                ImGui::TextDisabled("%s", JointName(skeleton, joint));
                ImGui::Separator();
                ImGui::BeginDisabled(session.IsReadOnly());
                if (state.selected != Bone::Count && mappedTo != state.selected &&
                    ImGui::MenuItem((std::string("Assign to ") + render::HumanoidBoneName(state.selected)).c_str()))
                    AssignToSelected(session, state, joint);
                if (ImGui::BeginMenu("Assign to"))
                {
                    for (const auto &part : GetHumanoidBodyParts())
                    {
                        ImGui::TextDisabled("%s", part.label);
                        for (const Bone bone : part.bones)
                        {
                            const int current = render::GetHumanoidTargetJoint(skeleton, bone);
                            if (ImGui::MenuItem(render::HumanoidBoneName(bone), current >= 0 ? JointName(skeleton, current) : nullptr, bone == mappedTo))
                            {
                                AssignJoint(session, state, bone, joint, false);
                                state.selected = bone;
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                if (mappedTo != Bone::Count && ImGui::MenuItem((std::string("Unassign from ") + render::HumanoidBoneName(mappedTo)).c_str()))
                    session.ModifyRig([&](render::Skeleton &edited) { AssignHumanoidJoint(edited, mappedTo, -1); });
                ImGui::EndDisabled();
                if (mappedTo != Bone::Count && ImGui::MenuItem((std::string("Select ") + render::HumanoidBoneName(mappedTo)).c_str()))
                    state.selected = mappedTo;
                ImGui::EndPopup();
            }
        }

        // `previewJoint` is a joint hovered elsewhere last frame (the joint picker), drawn highlighted.
        void DrawCanvas(MeshAssetSession &session, HumanoidRigViewState &state, const HumanoidRigAnalysisCache &rig, ImVec2 size, int previewJoint)
        {
            const auto &skeleton = session.GetSkeleton();
            const auto &diagram = state.diagram;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            size.x = (std::max)(size.x, 64.0f);
            size.y = (std::max)(size.y, 64.0f);
            const ImVec2 max(min.x + size.x, min.y + size.y);
            ImGui::InvisibleButton("##RigCanvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            const bool hovered = ImGui::IsItemHovered();
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            auto &io = ImGui::GetIO();

            CanvasTransform transform = MakeTransform(diagram, state, min, size);
            // Navigation: wheel zooms about the cursor, middle or right drag pans.
            if (hovered && io.MouseWheel != 0.0f)
            {
                const glm::vec2 before = transform.ToDiagram(io.MousePos);
                state.zoom = std::clamp(state.zoom * std::pow(1.15f, io.MouseWheel), 0.25f, 16.0f);
                transform = MakeTransform(diagram, state, min, size);
                state.pan += before - transform.ToDiagram(io.MousePos);
                transform = MakeTransform(diagram, state, min, size);
            }
            const bool panning = ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 2.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f));
            if (panning)
            {
                state.pan -= glm::vec2(io.MouseDelta.x, -io.MouseDelta.y) / transform.scale;
                transform = MakeTransform(diagram, state, min, size);
            }

            std::vector<ImVec2> points(diagram.joints.size());
            for (std::size_t index = 0; index < points.size(); ++index)
                points[index] = transform.ToScreen(diagram.joints[index]);

            std::vector<int> hits;
            if (hovered && !panning)
                hits = PickRigJoints(diagram, transform.ToDiagram(io.MousePos), kJointPickRadius / transform.scale);
            const int hoveredJoint = hits.empty() ? -1 : hits.front();
            const int highlightedJoint = state.highlightedJoint >= 0 ? state.highlightedJoint : previewJoint;

            // Clicks: a joint shared with others asks which one was meant.
            if (hoveredJoint >= 0 && ImGui::IsItemDeactivated() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left))
            {
                std::vector<int> shared;
                for (const int joint : hits)
                    if (glm::distance(diagram.joints[static_cast<std::size_t>(joint)], diagram.joints[static_cast<std::size_t>(hoveredJoint)]) * transform.scale < 2.5f)
                        shared.push_back(joint);
                if (shared.size() > 1)
                {
                    state.pickChoices = std::move(shared);
                    ImGui::OpenPopup("##ChooseJoint");
                }
                else
                    ClickJoint(session, state, hoveredJoint);
            }
            if (hoveredJoint >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Right, 4.0f))
            {
                state.contextJoint = hoveredJoint;
                ImGui::OpenPopup("##JointMenu");
            }
            if (focused && !io.WantTextInput)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                {
                    state.zoom = 1.0f;
                    state.pan = glm::vec2(0.0f);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    state.selected = Bone::Count;
                if ((ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) && state.selected != Bone::Count &&
                    !session.IsReadOnly())
                {
                    const Bone bone = state.selected;
                    session.ModifyRig([&](render::Skeleton &edited) { AssignHumanoidJoint(edited, bone, -1); });
                }
            }

            ImDrawList *drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(min, max, GetEditorColorU32(EditorColor::Background), 6.0f);
            drawList->AddRect(min, max, GetEditorColorU32(EditorColor::Border), 6.0f);
            drawList->PushClipRect(min, max, true);

            // Ground line at the lowest point.
            const float groundY = transform.ToScreen(diagram.min).y;
            drawList->AddLine(ImVec2(min.x, groundY), ImVec2(max.x, groundY), GetEditorColorU32(EditorColor::Border), 1.0f);
            DrawSilhouette(drawList, diagram, transform);

            const int selectedJoint = state.selected == Bone::Count ? -1 : render::GetHumanoidTargetJoint(skeleton, state.selected);
            std::vector<Bone> slotOf(skeleton.joints.size(), Bone::Count);
            for (std::size_t index = 0; index < render::kHumanoidBoneCount; ++index)
            {
                const auto bone = static_cast<Bone>(index);
                if (const int joint = render::GetHumanoidTargetJoint(skeleton, bone); joint >= 0 && slotOf[static_cast<std::size_t>(joint)] == Bone::Count)
                    slotOf[static_cast<std::size_t>(joint)] = bone;
            }

            // Bones: parent to child, coloured where both ends belong to the same body part.
            for (std::size_t index = 0; index < skeleton.joints.size() && index < points.size(); ++index)
            {
                const int parent = skeleton.joints[index].parentJointIndex;
                if (!render::IsValidSkeletonJoint(skeleton, parent))
                    continue;
                const Bone childBone = slotOf[index];
                const Bone parentBone = slotOf[static_cast<std::size_t>(parent)];
                const bool mappedSegment = childBone != Bone::Count && parentBone != Bone::Count;
                const ImU32 color = mappedSegment ? U32(BoneColor(childBone), 0.8f) : GetEditorColorU32(EditorColor::TextMuted, 0.45f);
                drawList->AddLine(points[static_cast<std::size_t>(parent)], points[index], color, mappedSegment ? 2.5f : 1.5f);
            }

            // Joints, far ones first.
            std::vector<int> order(points.size());
            std::iota(order.begin(), order.end(), 0);
            std::ranges::sort(order, [&](int a, int b) { return diagram.depth[static_cast<std::size_t>(a)] < diagram.depth[static_cast<std::size_t>(b)]; });
            for (const int joint : order)
            {
                const ImVec2 point = points[static_cast<std::size_t>(joint)];
                const Bone bone = slotOf[static_cast<std::size_t>(joint)];
                if (IsFollower(rig, joint))
                {
                    // Split-pelvis roots: a diamond in the body colour, they move with Hips.
                    const float r = 5.5f;
                    drawList->AddQuadFilled(ImVec2(point.x, point.y - r), ImVec2(point.x + r, point.y), ImVec2(point.x, point.y + r), ImVec2(point.x - r, point.y),
                                            U32(PartColor(HumanoidBodyPart::Body), 0.85f));
                }
                else if (bone != Bone::Count)
                {
                    drawList->AddCircleFilled(point, 5.0f, U32(BoneColor(bone)));
                    drawList->AddCircle(point, 5.0f, GetEditorColorU32(EditorColor::Background), 0, 1.5f);
                }
                else
                {
                    drawList->AddCircleFilled(point, 3.0f, GetEditorColorU32(EditorColor::Panel));
                    drawList->AddCircle(point, 3.0f, GetEditorColorU32(EditorColor::TextMuted, 0.8f), 0, 1.2f);
                }
            }
            if (selectedJoint >= 0)
            {
                const ImVec2 point = points[static_cast<std::size_t>(selectedJoint)];
                drawList->AddCircle(point, 9.0f, GetEditorColorU32(EditorColor::Accent), 0, 2.0f);
                drawList->AddCircle(point, 12.5f, GetEditorColorU32(EditorColor::Accent, 0.35f), 0, 1.5f);
            }
            for (const int joint : {hoveredJoint, highlightedJoint})
                if (joint >= 0 && joint != selectedJoint)
                    drawList->AddCircle(points[static_cast<std::size_t>(joint)], 8.0f, GetEditorColorU32(EditorColor::Text, 0.85f), 0, 1.5f);

            // Labels: the selected and hovered joints always, then mapped joints, then the rest when asked.
            std::vector<Label> labels;
            for (std::size_t index = 0; index < points.size(); ++index)
            {
                const int joint = static_cast<int>(index);
                const Bone bone = slotOf[index];
                const bool follower = IsFollower(rig, joint);
                int priority = 4;
                if (joint == selectedJoint)
                    priority = 0;
                else if (joint == hoveredJoint || joint == highlightedJoint)
                    priority = 1;
                else if (bone != Bone::Count)
                    priority = 2;
                else if (follower)
                    priority = 3;
                else if (!state.showAllJointNames)
                    continue;
                Label label;
                label.joint = joint;
                label.priority = priority;
                label.name = skeleton.joints[index].name;
                if (bone != Bone::Count)
                {
                    label.slot = render::HumanoidBoneName(bone);
                    label.color = BoneColor(bone);
                }
                else if (follower)
                {
                    label.slot = "Follows Hips";
                    label.color = PartColor(HumanoidBodyPart::Body);
                }
                else
                    label.color = GetEditorColor(EditorColor::TextMuted);
                labels.push_back(std::move(label));
            }
            DrawLabels(drawList, points, std::move(labels), ImVec2(min.x + 4.0f, min.y + 30.0f), ImVec2(max.x - 4.0f, max.y - 24.0f));

            // Instructions in the top-left corner, a key in the bottom-left.
            std::string hint;
            if (!state.notice.empty())
                hint = state.notice;
            else if (session.IsReadOnly())
                hint = "Read-only. Click a joint to see its bone.";
            else if (skeleton.humanoidBoneMappings.empty())
                hint = "Create a humanoid map to assign these joints to bones.";
            else if (state.selected != Bone::Count)
                hint = std::string("Click a joint to assign it to ") + render::HumanoidBoneName(state.selected) + ".  Esc to deselect.";
            else
                hint = "Select a bone in the list, then click its joint. Click a mapped joint to select its bone.";
            const ImVec2 hintPos(min.x + 10.0f, min.y + 8.0f);
            const ImVec2 hintSize = ImGui::CalcTextSize(hint.c_str());
            drawList->AddRectFilled(ImVec2(hintPos.x - 6.0f, hintPos.y - 3.0f), ImVec2(hintPos.x + hintSize.x + 6.0f, hintPos.y + hintSize.y + 3.0f),
                                    GetEditorColorU32(EditorColor::Panel, 0.85f), 4.0f);
            drawList->AddText(hintPos, GetEditorColorU32(state.notice.empty() ? EditorColor::Text : EditorColor::Accent), hint.c_str());

            const float keyY = max.y - ImGui::GetTextLineHeight() - 8.0f;
            float keyX = min.x + 12.0f;
            const auto key = [&](const char *text, auto drawMark)
            {
                drawMark(ImVec2(keyX, keyY + ImGui::GetTextLineHeight() * 0.5f));
                keyX += 10.0f;
                drawList->AddText(ImVec2(keyX, keyY), GetEditorColorU32(EditorColor::TextMuted), text);
                keyX += ImGui::CalcTextSize(text).x + 16.0f;
            };
            key("Assigned", [&](ImVec2 at) { drawList->AddCircleFilled(at, 4.0f, U32(PartColor(HumanoidBodyPart::Body))); });
            key("Unassigned", [&](ImVec2 at) { drawList->AddCircle(at, 3.0f, GetEditorColorU32(EditorColor::TextMuted), 0, 1.2f); });
            if (!rig.status.hipsFollowers.empty())
                key("Split pelvis root", [&](ImVec2 at)
                    { drawList->AddQuadFilled(ImVec2(at.x, at.y - 4.5f), ImVec2(at.x + 4.5f, at.y), ImVec2(at.x, at.y + 4.5f), ImVec2(at.x - 4.5f, at.y),
                                              U32(PartColor(HumanoidBodyPart::Body), 0.85f)); });
            const char *view = state.view == RigView::Front ? "Front view: the character's left is on your right" : "Side view: facing right";
            const ImVec2 viewSize = ImGui::CalcTextSize(view);
            if (max.x - viewSize.x - 12.0f > keyX)
                drawList->AddText(ImVec2(max.x - viewSize.x - 12.0f, keyY), GetEditorColorU32(EditorColor::TextMuted, 0.8f), view);
            drawList->PopClipRect();

            if (hoveredJoint >= 0 && !panning && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(JointName(skeleton, hoveredJoint));
                const int parent = skeleton.joints[static_cast<std::size_t>(hoveredJoint)].parentJointIndex;
                ImGui::TextDisabled("Parent: %s", render::IsValidSkeletonJoint(skeleton, parent) ? JointName(skeleton, parent) : "none (root)");
                if (const Bone bone = slotOf[static_cast<std::size_t>(hoveredJoint)]; bone != Bone::Count)
                    ImGui::TextColored(BoneColor(bone), "%s", render::HumanoidBoneName(bone));
                else if (IsFollower(rig, hoveredJoint))
                    ImGui::TextDisabled("Split pelvis root: moves with Hips");
                if (hits.size() > 1)
                    ImGui::TextDisabled("%zu joints here; click to choose", hits.size());
                ImGui::EndTooltip();
            }
            DrawJointMenus(session, state);
        }

        // ---------------------------------------------------------------- details

        void DrawJointPicker(MeshAssetSession &session, HumanoidRigViewState &state, Bone bone, int current)
        {
            const auto &skeleton = session.GetSkeleton();
            ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x));
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 24.0f));
            if (ImGui::BeginCombo("##Joint", current >= 0 ? JointName(skeleton, current) : "Unassigned", ImGuiComboFlags_HeightLargest))
            {
                if (ImGui::IsWindowAppearing())
                {
                    state.jointSearch.clear();
                    ImGui::SetKeyboardFocusHere();
                }
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##JointSearch", (std::string(icons::Search) + "  Search joints").c_str(), &state.jointSearch);
                if (ImGui::Selectable("Unassigned", current < 0))
                    session.ModifyRig([&](render::Skeleton &edited) { AssignHumanoidJoint(edited, bone, -1); });
                const bool searching = !state.jointSearch.empty();
                for (const auto &[joint, depth] : state.jointTree)
                {
                    const char *name = JointName(skeleton, joint);
                    if (searching && !ContainsInsensitive(name, state.jointSearch))
                        continue;
                    ImGui::PushID(joint);
                    if (!searching)
                        ImGui::Indent(static_cast<float>(depth) * 10.0f + 0.01f);
                    const Bone other = FindHumanoidBoneForJoint(skeleton, joint);
                    if (ImGui::Selectable(name, joint == current))
                        AssignJoint(session, state, bone, joint, false);
                    if (ImGui::IsItemHovered())
                        state.highlightedJoint = joint;
                    if (other != Bone::Count && other != bone)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(BoneColor(other), "%s", render::HumanoidBoneName(other));
                        ImGui::SetItemTooltip("Choosing it here moves it from %s.", render::HumanoidBoneName(other));
                    }
                    if (!searching)
                        ImGui::Unindent(static_cast<float>(depth) * 10.0f + 0.01f);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(current < 0);
            if (widgets::IconButton("##ClearJoint", icons::Close, "Unassign (Delete)"))
                session.ModifyRig([&](render::Skeleton &edited) { AssignHumanoidJoint(edited, bone, -1); });
            ImGui::EndDisabled();
        }

        // A labelled control in the stacked details form; the help shows on hovering the label.
        void FieldLabel(const char *label, const char *help)
        {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            if (help)
                ImGui::SetItemTooltip("%s", help);
        }

        void DrawSlotDetails(MeshAssetSession &session, HumanoidRigViewState &state, const HumanoidRigAnalysisCache &rig)
        {
            const auto &skeleton = session.GetSkeleton();
            const Bone bone = state.selected;
            if (bone == Bone::Count)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
                ImGui::TextWrapped("Select a bone in the list, or click a mapped joint in the diagram, to see its settings.");
                ImGui::PopStyleColor();
                return;
            }

            const render::HumanoidBoneMapping fallback{.bone = bone, .copyTranslation = bone == Bone::Hips};
            const auto *found = render::FindHumanoidMapping(skeleton, bone);
            const render::HumanoidBoneMapping mapping = found ? *found : fallback;
            const int joint = render::GetHumanoidTargetJoint(skeleton, bone);
            const auto edit = [&](const std::function<void(render::HumanoidBoneMapping &)> &change)
            {
                session.ModifyRig([&](render::Skeleton &edited)
                                  {
                                      render::EnsureHumanoidMappings(edited);
                                      change(*render::FindHumanoidMapping(edited, bone));
                                  });
            };

            // Heading: the bone in its colour, required or optional.
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.12f);
            ImGui::TextColored(BoneColor(bone), "%s", render::HumanoidBoneName(bone));
            ImGui::PopFont();
            ImGui::SameLine();
            widgets::Pill(render::IsRequiredHumanoidBone(bone) ? "Required" : "Optional",
                          render::IsRequiredHumanoidBone(bone) ? GetEditorColor(EditorColor::Accent) : GetEditorColor(EditorColor::TextMuted));
            for (const auto *problem : rig.ProblemsFor(bone))
            {
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
                ImGui::TextWrapped("%s  %s", icons::Warning, problem->message.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::BeginDisabled(session.IsReadOnly());
            FieldLabel("Joint", "The mesh's joint this bone drives. You can also click it in the diagram.");
            DrawJointPicker(session, state, bone, joint);
            if (const Bone mirror = MirrorHumanoidBone(bone); mirror != bone)
            {
                const int mirrored = FindMirroredJoint(skeleton, render::GetHumanoidTargetJoint(skeleton, mirror));
                if (mirrored >= 0 && mirrored != joint)
                {
                    if (ImGui::SmallButton((std::string("Use ") + JointName(skeleton, mirrored) + ", mirroring " + render::HumanoidBoneName(mirror)).c_str()))
                        AssignJoint(session, state, bone, mirrored, false);
                    ImGui::SetItemTooltip("The other side's joint with the side swapped in its name.");
                }
            }

            if (bone == Bone::Hips && !rig.status.hipsFollowers.empty())
            {
                std::string roots;
                for (const int follower : rig.status.hipsFollowers)
                    roots += (roots.empty() ? "" : ", ") + std::string(JointName(skeleton, follower));
                FieldLabel("Split pelvis", nullptr);
                ImGui::TextWrapped("%s %s separate skeleton %s with no slot, and %s rigidly with Hips.", roots.c_str(),
                                   rig.status.hipsFollowers.size() == 1 ? "is a" : "are", rig.status.hipsFollowers.size() == 1 ? "root" : "roots",
                                   rig.status.hipsFollowers.size() == 1 ? "moves" : "move");
            }

            FieldLabel("Source name (optional)", "Set it when clips name this bone in a way the automatic Mixamo, Unreal, Rigify and common names miss.");
            std::string source = mapping.sourceBoneName;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##SourceName", "Automatic", &source))
                edit([&](render::HumanoidBoneMapping &changed) { changed.sourceBoneName = source; });

            FieldLabel("Rotation offset", "Extra rotation after retargeting, for a bone that still points the wrong way.");
            glm::vec3 offset = mapping.rotationOffsetDegrees;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat3("##RotationOffset", &offset.x, 0.25f, -180.0f, 180.0f, "%.1f deg"))
                edit([&](render::HumanoidBoneMapping &changed) { changed.rotationOffsetDegrees = offset; });

            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            bool copy = mapping.copyTranslation;
            if (widgets::ToggleSwitch("##CopyTranslation", &copy))
                edit([&](render::HumanoidBoneMapping &changed) { changed.copyTranslation = copy; });
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Copy translation");
            ImGui::SetItemTooltip("%s", bone == Bone::Hips ? "Move the body with the clip's pelvis. Usually on for Hips."
                                                           : "Usually off: limbs keep this mesh's lengths.");
            if (mapping.copyTranslation)
            {
                FieldLabel("Translation scale", "Scales copied movement, for a source character of a different height.");
                float scale = mapping.translationScale;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::DragFloat("##TranslationScale", &scale, 0.01f, 0.0f, 10.0f, "%.2f"))
                    edit([&](render::HumanoidBoneMapping &changed) { changed.translationScale = scale; });
            }
            ImGui::EndDisabled();
        }

        // Every problem, each selecting its slot when clicked.
        void DrawProblems(HumanoidRigViewState &state, const HumanoidRigAnalysisCache &rig)
        {
            for (int index = 0; index < static_cast<int>(rig.status.problems.size()); ++index)
            {
                const auto &problem = rig.status.problems[static_cast<std::size_t>(index)];
                ImGui::PushID(index);
                ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
                const std::string text = std::string(icons::Warning) + "  " + problem.message;
                if (ImGui::Selectable(text.c_str()) && problem.bone != Bone::Count)
                {
                    state.selected = problem.bone;
                    state.notice.clear();
                }
                ImGui::PopStyleColor();
                if (problem.bone != Bone::Count)
                    ImGui::SetItemTooltip("Select %s", render::HumanoidBoneName(problem.bone));
                ImGui::PopID();
            }
        }

        void DrawEmptyMap(MeshAssetSession &session, HumanoidRigViewState &state)
        {
            widgets::BeginCard("##NoHumanoidMap");
            ImGui::TextWrapped("A humanoid map tells clips which of this mesh's joints are its hips, spine, arms and legs, so clips made for "
                               "other characters (Mixamo, Unreal, other rigs) play on it even when the joint names or order differ.");
            ImGui::Spacing();
            ImGui::BeginDisabled(session.IsReadOnly());
            if (widgets::PrimaryButton("Create Humanoid Map"))
            {
                session.ModifyRig(render::AutoMapHumanoidRig);
                // Start where the user is most likely needed.
                const Bone empty = NextUnassignedHumanoidBone(session.GetSkeleton(), Bone::Count);
                state.selected = empty != Bone::Count ? empty : Bone::Hips;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("Starts from joint names and the hierarchy; you can then adjust each bone.");
            widgets::EndCard();
        }
    }

    void HumanoidRigAnalysisCache::Update(const MeshAssetSession &session)
    {
        if (revision == session.GetRevision())
            return;
        revision = session.GetRevision();
        status = render::AnalyzeHumanoidRig(session.GetSkeleton());
    }

    std::vector<const render::HumanoidRigProblem *> HumanoidRigAnalysisCache::ProblemsFor(Bone bone) const
    {
        std::vector<const render::HumanoidRigProblem *> problems;
        for (const auto &problem : status.problems)
            if (problem.bone == bone)
                problems.push_back(&problem);
        return problems;
    }

    void DrawHumanoidRigPage(MeshAssetSession &session, HumanoidRigViewState &state, const HumanoidRigAnalysisCache &rig)
    {
        const auto &skeleton = session.GetSkeleton();
        widgets::PageTitle("Humanoid Rig", "Match this mesh's joints to humanoid bones so clips made for other characters play on it.");
        if (skeleton.joints.empty())
        {
            widgets::BeginCard("##NoSkeleton");
            ImGui::TextDisabled("This mesh has no skeleton, so it has nothing to retarget.");
            widgets::EndCard();
            return;
        }
        RefreshDerived(session, state);
        const int previewJoint = std::exchange(state.highlightedJoint, -1);

        if (skeleton.humanoidBoneMappings.empty())
        {
            DrawEmptyMap(session, state);
            ImGui::Spacing();
            if (skeleton.humanoidBoneMappings.empty())
            {
                state.selected = Bone::Count;
                DrawCanvas(session, state, rig, ImGui::GetContentRegionAvail(), previewJoint);
            }
            return;
        }

        DrawRigToolbar(session, state, rig);
        DrawProblems(state, rig);
        ImGui::Spacing();

        // Slots | diagram | details. The details go under the diagram when the panel is narrow.
        const float available = ImGui::GetContentRegionAvail().y;
        const ImGuiStyle &style = ImGui::GetStyle();
        ImGui::BeginChild("##Slots", ImVec2(ImGui::GetFontSize() * 15.0f, available), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        DrawSlotList(session, state, rig);
        ImGui::EndChild();
        ImGui::SameLine();

        const float area = ImGui::GetContentRegionAvail().x;
        const float detailsWidth = ImGui::GetFontSize() * 19.0f;
        const bool besideDiagram = area >= detailsWidth + ImGui::GetFontSize() * 24.0f;
        const ImGuiChildFlags detailsFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, GetEditorColor(EditorColor::Panel));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        if (besideDiagram)
        {
            DrawCanvas(session, state, rig, ImVec2(area - detailsWidth - style.ItemSpacing.x, available), previewJoint);
            ImGui::SameLine();
            ImGui::BeginChild("##SlotDetails", ImVec2(0.0f, available), detailsFlags);
        }
        else
        {
            ImGui::BeginGroup();
            const float canvasHeight = (std::max)(ImGui::GetFontSize() * 14.0f, available * 0.6f);
            DrawCanvas(session, state, rig, ImVec2(area, canvasHeight), previewJoint);
            ImGui::BeginChild("##SlotDetails", ImVec2(area, (std::max)(ImGui::GetFrameHeight() * 3.0f, available - canvasHeight - style.ItemSpacing.y)),
                              detailsFlags);
        }
        ImGui::PopStyleVar();
        DrawSlotDetails(session, state, rig);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (!besideDiagram)
            ImGui::EndGroup();
    }
}
