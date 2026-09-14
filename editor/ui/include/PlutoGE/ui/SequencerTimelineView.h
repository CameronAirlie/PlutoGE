#pragma once
#include "PlutoGE/ui/TimelineEditing.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include <imgui_internal.h>
#include <ImSequencer.h>

namespace PlutoGE::ui
{
    inline constexpr const char *TimelineChannelLabels[] = {"Position", "Rotation (Euler)", "Scale", "Camera FOV", "Light Color", "Light Intensity", "Audio Volume", "Audio Play", "Script Event"};
    class SequencerTimelineView final : public ImSequencer::SequenceInterface
    {
    public:
        SequencerTimelineView(scene::Timeline &data, scene::Scene &scene, int &track, int &key,
                     int &dragTrack, int &dragKey, bool &dirty)
            : data(data), selectedTrack(track), selectedKey(key), dragTrack(dragTrack), dragKey(dragKey), dirty(dirty)
        {
            for (const auto &trackData : data.tracks)
            {
                const auto *target = scene.FindEntityByID(trackData.entity);
                labels.push_back((target ? target->GetName() : "Missing target") + " / " + TimelineChannelLabels[static_cast<int>(trackData.channel)]);
                starts.push_back(trackData.keys.empty() ? 0 : static_cast<int>(std::round(trackData.keys.front().time * 60)));
                ends.push_back(trackData.keys.empty() ? 0 : static_cast<int>(std::round(trackData.keys.back().time * 60)));
            }
        }
        int GetFrameMin() const override { return 0; }
        int GetFrameMax() const override { return std::max(1, static_cast<int>(std::round(data.duration * 60))); }
        int GetItemCount() const override { return static_cast<int>(data.tracks.size()); }
        const char *GetItemLabel(int index) const override { return labels[index].c_str(); }
        void Get(int index, int **start, int **end, int *type, unsigned *color) override
        {
            if (start) *start = &starts[index];
            if (end) *end = &ends[index];
            if (type) *type = 0;
            if (color) *color = 0xff785038;
        }
        void CustomDrawCompact(int index, ImDrawList *draw, const ImRect &rect, const ImRect &clip) override
        {
            // The canvas InvisibleButton owns ActiveId on mouse-down. Allow that
            // active item while retaining popup and window clipping checks.
            const ImRect legend({clip.Min.x - 200.0f, rect.Min.y}, {clip.Min.x, rect.Max.y});
            if (ImGui::GetCurrentWindow()->ClipRect.Contains(ImGui::GetMousePos()) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                (legend.Contains(ImGui::GetMousePos()) || clip.Contains(ImGui::GetMousePos())) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                selectedTrack = index; selectedKey = -1;
            }
            const float scale = rect.GetWidth() / static_cast<float>(GetFrameMax() + 2);
            if (scale <= 0) return;
            draw->PushClipRect(clip.Min, clip.Max, true);
            auto &track = data.tracks[index];
            for (std::size_t key = 0; key < track.keys.size(); ++key)
            {
                const float x = rect.Min.x + (static_cast<float>(track.keys[key].time * 60.0) + .5f) * scale;
                const float y = (rect.Min.y + rect.Max.y) * .5f;
                if (ImGui::GetCurrentWindow()->ClipRect.Contains(ImGui::GetMousePos()) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && clip.Contains(ImGui::GetMousePos()) &&
                    ImRect({x - 6, y - 8}, {x + 6, y + 8}).Contains(ImGui::GetMousePos()))
                {
                    ImGui::SetTooltip("%.3f s (frame %d)", track.keys[key].time, static_cast<int>(std::round(track.keys[key].time * 60)));
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        selectedTrack = dragTrack = index;
                        selectedKey = dragKey = static_cast<int>(key);
                    }
                }
                const auto color = selectedTrack == index && selectedKey == static_cast<int>(key) ? 0xff60d8ff : 0xffeeeeee;
                draw->AddQuadFilled({x, y - 5}, {x + 5, y}, {x, y + 5}, {x - 5, y}, color);
            }
            if (dragTrack == index && dragKey >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const double frame = std::round((ImGui::GetMousePos().x - rect.Min.x) / scale - .5f);
                dirty |= MoveTimelineKey(track, static_cast<std::size_t>(dragKey), frame / 60.0, data.duration);
            }
            draw->PopClipRect();
        }
    private:
        scene::Timeline &data;
        int &selectedTrack, &selectedKey, &dragTrack, &dragKey;
        bool &dirty;
        std::vector<std::string> labels;
        std::vector<int> starts, ends;
    };
}
