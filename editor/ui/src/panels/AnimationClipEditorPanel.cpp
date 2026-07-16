#include "PlutoGE/ui/panels/AnimationClipEditorPanel.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/EditorShell.h"

#include <ImSequencer.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kFramesPerSecond = 60.0f;

        class EventSequence final : public ImSequencer::SequenceInterface
        {
        public:
            explicit EventSequence(render::AnimationClip &clip)
                : m_clip(clip), m_frames(clip.events.size())
            {
                for (std::size_t index = 0; index < clip.events.size(); ++index)
                {
                    m_frames[index] = std::max(0, static_cast<int>(std::lround(clip.events[index].time * kFramesPerSecond)));
                }
            }

            int GetFrameMin() const override { return 0; }
            int GetFrameMax() const override
            {
                return std::max(1, static_cast<int>(std::ceil(m_clip.duration * kFramesPerSecond)));
            }
            int GetItemCount() const override { return static_cast<int>(m_clip.events.size()); }
            int GetItemTypeCount() const override { return 1; }
            const char *GetItemTypeName(int) const override { return "Event"; }
            const char *GetItemLabel(int index) const override
            {
                return index >= 0 && index < static_cast<int>(m_clip.events.size())
                           ? m_clip.events[static_cast<std::size_t>(index)].name.c_str()
                           : "";
            }
            void Get(int index, int **start, int **end, int *type, unsigned int *color) override
            {
                if (index < 0 || index >= static_cast<int>(m_frames.size()))
                    return;
                auto &frame = m_frames[static_cast<std::size_t>(index)];
                m_clip.events[static_cast<std::size_t>(index)].time =
                    std::clamp(frame / kFramesPerSecond, 0.0f, m_clip.duration);
                if (start)
                    *start = &frame;
                if (end)
                    *end = &frame;
                if (type)
                    *type = 0;
                if (color)
                    *color = 0xff58b8d8;
            }
            void Add(int) override
            {
                m_clip.events.push_back({.time = 0.0f, .name = "AnimationEvent"});
                m_frames.push_back(0);
            }
            void Del(int index) override
            {
                if (index < 0 || index >= static_cast<int>(m_frames.size()))
                    return;
                m_clip.events.erase(m_clip.events.begin() + index);
                m_frames.erase(m_frames.begin() + index);
            }
            void Duplicate(int index) override
            {
                if (index < 0 || index >= static_cast<int>(m_frames.size()))
                    return;
                m_clip.events.insert(m_clip.events.begin() + index + 1, m_clip.events[static_cast<std::size_t>(index)]);
                m_frames.insert(m_frames.begin() + index + 1, m_frames[static_cast<std::size_t>(index)]);
            }

        private:
            render::AnimationClip &m_clip;
            std::vector<int> m_frames;
        };
    }

    void AnimationClipEditorPanel::LoadActiveClip()
    {
        auto &editor = EditorShell::GetInstance();
        m_loadedReference = editor.GetActiveAnimationClipAssetReference();
        m_clip = {};
        if (!m_loadedReference.empty())
            core::Engine::GetInstance().GetAssetManager().LoadAnimationClipAsset(m_loadedReference, m_clip);
        m_currentFrame = 0;
        m_firstFrame = 0;
        m_selectedEvent = -1;
        m_dirty = false;
    }

    void AnimationClipEditorPanel::Render()
    {
        auto &editor = EditorShell::GetInstance();
        const auto &reference = editor.GetActiveAnimationClipAssetReference();
        if (reference != m_loadedReference)
            LoadActiveClip();
        if (reference.empty())
        {
            ImGui::TextDisabled("Open an animation clip from the Content Browser.");
            return;
        }

        ImGui::TextUnformatted(reference.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%.3fs  |  60 fps", m_clip.duration);

        if (ImGui::Button("+ Add Event"))
        {
            const float eventTime = std::clamp(m_currentFrame / kFramesPerSecond, 0.0f, m_clip.duration);
            m_clip.events.push_back({.time = eventTime, .name = "AnimationEvent"});
            m_selectedEvent = static_cast<int>(m_clip.events.size()) - 1;
            m_dirty = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Playhead: %.3f s", m_currentFrame / kFramesPerSecond);

        if (m_clip.events.empty())
        {
            const ImVec2 emptySize(ImGui::GetContentRegionAvail().x, 150.0f);
            ImGui::BeginChild("EmptyAnimationEventTimeline", emptySize, ImGuiChildFlags_Borders);
            const char *message = "No animation events\n\nClick + Add Event to create the first marker.";
            const ImVec2 messageSize = ImGui::CalcTextSize(message);
            const ImVec2 available = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPos(ImVec2(
                std::max(0.0f, (available.x - messageSize.x) * 0.5f),
                std::max(0.0f, (available.y - messageSize.y) * 0.5f)));
            ImGui::TextDisabled("%s", message);
            ImGui::EndChild();
        }
        else
        {
            EventSequence sequence(m_clip);
            const auto before = m_clip.events;
            ImSequencer::Sequencer(&sequence, &m_currentFrame, &m_expanded, &m_selectedEvent, &m_firstFrame,
                                   ImSequencer::SEQUENCER_EDIT_STARTEND |
                                       ImSequencer::SEQUENCER_ADD |
                                       ImSequencer::SEQUENCER_DEL |
                                       ImSequencer::SEQUENCER_COPYPASTE);
            m_dirty |= before != m_clip.events;
        }

        if (m_selectedEvent >= 0 && m_selectedEvent < static_cast<int>(m_clip.events.size()))
        {
            auto &event = m_clip.events[static_cast<std::size_t>(m_selectedEvent)];
            ImGui::SeparatorText("Selected Event");
            char name[128]{};
            char text[256]{};
            strncpy_s(name, event.name.c_str(), _TRUNCATE);
            strncpy_s(text, event.stringParameter.c_str(), _TRUNCATE);
            if (ImGui::InputText("Name", name, sizeof(name)))
            {
                event.name = name;
                m_dirty = true;
            }
            if (ImGui::DragFloat("Time", &event.time, 0.01f, 0.0f, m_clip.duration, "%.3f s"))
            {
                event.time = std::clamp(event.time, 0.0f, m_clip.duration);
                m_dirty = true;
            }
            if (ImGui::InputText("String Parameter", text, sizeof(text)))
            {
                event.stringParameter = text;
                m_dirty = true;
            }
            m_dirty |= ImGui::DragFloat("Float Parameter", &event.floatParameter, 0.01f);
            m_dirty |= ImGui::DragInt("Int Parameter", &event.intParameter);
        }

        ImGui::Separator();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Save"))
        {
            std::sort(m_clip.events.begin(), m_clip.events.end(),
                      [](const auto &left, const auto &right) { return left.time < right.time; });
            std::string error;
            if (core::Engine::GetInstance().GetAssetManager().SaveAnimationClipAsset(reference, m_clip, &error))
            {
                m_dirty = false;
                editor.MarkProjectDirty();
                editor.Log(EditorShell::ConsoleSeverity::Info, "Saved animation clip events: " + reference);
            }
            else
                editor.Log(EditorShell::ConsoleSeverity::Error, error);
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert"))
            LoadActiveClip();
        ImGui::EndDisabled();
    }
}
