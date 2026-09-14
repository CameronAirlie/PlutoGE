#include "PlutoGE/ui/panels/SequencerEditorPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/TimelineEditing.h"
#include "PlutoGE/ui/SequencerTimelineView.h"
#include "PlutoGE/scene/components/SequencerComponent.h"
#include "PlutoGE/scene/TimelineBindings.h"
#include <imgui_internal.h>
#include <ImSequencer.h>
#include <cstdio>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr double Fps = 60;
        constexpr auto &Channels = TimelineChannelLabels;
        int Frame(double time) { return static_cast<int>(std::round(time * Fps)); }


        void EntityOptions(scene::Entity &entity, std::uint32_t &binding, bool &edited)
        {
            ImGui::PushID(static_cast<int>(entity.GetID()));
            if (ImGui::Selectable(entity.GetName().c_str(), binding == entity.GetID())) { binding = entity.GetID(); edited = true; }
            ImGui::PopID();
            for (auto *child : entity.GetChildren()) EntityOptions(*child, binding, edited);
        }
    }

    void SequencerEditorPanel::Render()
    {
        auto &editor = EditorShell::GetInstance();
        auto *owner = editor.GetSelectedEntity();
        auto *sequence = owner ? owner->GetComponent<scene::SequencerComponent>() : nullptr;
        if (!sequence || !owner->GetScene())
        {
            m_owner = 0; m_dirty = false;
            ImGui::TextDisabled("Select an entity with a Sequencer component.");
            return;
        }
        auto &scene = *owner->GetScene();
        if (scene.IsRuntimeStarted())
        {
            ImGui::TextDisabled("Stop Play mode to edit sequences.");
            return;
        }
        const auto source = sequence->GetTimeline().Encode();
        if (m_owner != owner->GetID() || m_source != source || m_sceneRevision != editor.GetSceneRevision())
        {
            if (m_owner != owner->GetID() || m_sceneRevision != editor.GetSceneRevision())
            {
                editor.GetTimelinePreview().Stop(); m_frame = m_firstFrame = 0;
            }
            m_sceneRevision = editor.GetSceneRevision();
            m_owner = owner->GetID(); m_source = source; m_draft = sequence->GetTimeline();
            m_track = m_key = m_dragTrack = m_dragKey = -1;
            m_dirty = false; m_error.clear();
        }
        ImGui::Text("%s | 60 fps | %zu tracks", owner->GetName().c_str(), m_draft.tracks.size());
        auto &preview = editor.GetTimelinePreview();
        const bool canResume = preview.Owner() == m_owner && !preview.IsPlaying() && preview.Time() < sequence->GetTimeline().duration;
        ImGui::BeginDisabled(preview.Owner() == m_owner && preview.IsPlaying());
        if (ImGui::Button(canResume ? "Resume" : "Play Preview"))
        {
            if (!canResume || !preview.Resume()) preview.Begin(scene, m_owner, true);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(preview.Owner() != m_owner || !preview.IsPlaying());
        if (ImGui::Button("Pause")) preview.Pause();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Stop")) { preview.Stop(); m_frame = 0; }
        ImGui::SameLine();
        ImGui::TextDisabled("Preview mutes audio and script events.");
        if (preview.Owner() == m_owner && preview.IsPlaying()) m_frame = Frame(preview.Time());
        const int previousFrame = m_frame;
        ImGui::SetNextItemWidth(140);
        ImGui::InputInt("Frame", &m_frame);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        m_dirty |= ImGui::InputDouble("Duration (s)", &m_draft.duration, .1, 1, "%.3f");
        // Keep the ImSequencer integer domain finite while duration text is edited.
        if (!std::isfinite(m_draft.duration)) m_draft.duration = 1;
        m_draft.duration = std::clamp(m_draft.duration, 1.0 / Fps, 86400.0);
        ImGui::SameLine(); m_dirty |= ImGui::Checkbox("Loop", &m_draft.loop);
        if (ImGui::Button("Add Track") && m_draft.tracks.size() < 256)
        {
            scene::TimelineTrack track; track.entity = m_owner;
            m_draft.tracks.push_back(track); m_track = static_cast<int>(m_draft.tracks.size() - 1); m_key = -1; m_dirty = true;
        }
        if (m_track >= 0 && m_track < static_cast<int>(m_draft.tracks.size()))
        {
            ImGui::SameLine();
            if (ImGui::Button("Duplicate Track") && m_draft.tracks.size() < 256)
            {
                auto copy = m_draft.tracks[m_track]; m_draft.tracks.push_back(std::move(copy)); m_dirty = true;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_track == 0);
            if (ImGui::Button("Move Track Up"))
            {
                std::swap(m_draft.tracks[m_track], m_draft.tracks[m_track - 1]); --m_track; m_dirty = true;
            }
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(m_track + 1 == static_cast<int>(m_draft.tracks.size()));
            if (ImGui::Button("Move Track Down"))
            {
                std::swap(m_draft.tracks[m_track], m_draft.tracks[m_track + 1]); ++m_track; m_dirty = true;
            }
            ImGui::EndDisabled(); ImGui::SameLine();
            if (ImGui::Button("Delete Track"))
            {
                m_draft.tracks.erase(m_draft.tracks.begin() + m_track); m_track = m_key = -1; m_dirty = true;
            }
        }
        ImGui::Separator();
        const std::string selectedLabel = m_track >= 0 && m_track < static_cast<int>(m_draft.tracks.size())
            ? "Track " + std::to_string(m_track + 1) : "Select a track";
        if (ImGui::BeginCombo("Track", selectedLabel.c_str()))
        {
            for (int index = 0; index < static_cast<int>(m_draft.tracks.size()); ++index)
            {
                const auto &track = m_draft.tracks[index];
                auto *target = scene.FindEntityByID(track.entity);
                const auto label = std::to_string(index + 1) + ": " + (target ? target->GetName() : "Missing target") + " / " + Channels[static_cast<int>(track.channel)];
                if (ImGui::Selectable(label.c_str(), m_track == index)) { m_track = index; m_key = -1; }
            }
            ImGui::EndCombo();
        }
        if (m_track >= 0 && m_track < static_cast<int>(m_draft.tracks.size()))
        {
            auto &track = m_draft.tracks[m_track];
            auto *target = scene.FindEntityByID(track.entity);
            if (ImGui::BeginCombo("Target", target ? target->GetName().c_str() : "Missing target"))
            {
                for (auto *root : scene.GetRootEntities()) EntityOptions(*root, track.entity, m_dirty);
                ImGui::EndCombo();
            }
            int channel = static_cast<int>(track.channel);
            if (ImGui::Combo("Channel", &channel, Channels, 9)) { track.channel = static_cast<scene::TimelineChannel>(channel); m_dirty = true; }
            int interpolation = static_cast<int>(track.interpolation);
            if (ImGui::Combo("Interpolation", &interpolation, "Step\0Linear\0Smooth\0")) { track.interpolation = static_cast<scene::TimelineInterpolation>(interpolation); m_dirty = true; }
        }
        ImGui::BeginChild("TimelineCanvas", ImVec2(0, 220), ImGuiChildFlags_Borders);
        SequencerTimelineView view(m_draft, scene, m_track, m_key, m_dragTrack, m_dragKey, m_dirty);
        const int previousTrack = m_track;
        ImSequencer::Sequencer(&view, &m_frame, &m_expanded, &m_track, &m_firstFrame, ImSequencer::SEQUENCER_CHANGE_FRAME);
        ImGui::EndChild();
        if (m_track != previousTrack && m_dragTrack < 0) m_key = -1;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_dragTrack = m_dragKey = -1;
        m_frame = std::clamp(m_frame, 0, Frame(m_draft.duration));
        if (m_frame != previousFrame)
        {
            if (preview.Owner() != m_owner) preview.Begin(scene, m_owner, false);
            preview.Seek(std::min(m_draft.duration, m_frame / Fps));
        }
        if (m_track >= 0 && m_track < static_cast<int>(m_draft.tracks.size()))
        {
            auto &track = m_draft.tracks[m_track];
            if (ImGui::Button("Add Key at Playhead"))
            {
                scene::TimelineKey key; key.time = std::min(m_draft.duration, m_frame / Fps);
                scene::ReadTimelineValue(scene, track, key.value);
                const int inserted = InsertTimelineKey(track, key, m_draft.duration);
                if (inserted >= 0) { m_key = inserted; m_dirty = true; }
                else m_error = "A key already exists at the playhead, or the key limit was reached.";
            }
            if (m_key >= 0 && m_key < static_cast<int>(track.keys.size()))
            {
                auto &key = track.keys[m_key];
                double time = key.time;
                if (ImGui::InputDouble("Key Time (s)", &time, 1.0 / Fps, .1, "%.6f")) m_dirty |= MoveTimelineKey(track, m_key, time, m_draft.duration);
                const bool event = track.channel == scene::TimelineChannel::AudioPlay || track.channel == scene::TimelineChannel::ScriptEvent;
                if (event)
                {
                    if (track.channel == scene::TimelineChannel::ScriptEvent)
                        m_dirty |= ImGui::InputFloat("Event Parameter", key.value.data());
                    char text[1025]{}; std::snprintf(text, sizeof(text), "%s", key.event.c_str());
                    if (ImGui::InputText("Event", text, sizeof(text))) { key.event = text; m_dirty = true; }
                }
                else
                {
                    if (track.channel == scene::TimelineChannel::LightColor) m_dirty |= ImGui::ColorEdit3("Value", key.value.data());
                    else if (track.channel <= scene::TimelineChannel::Scale) m_dirty |= ImGui::InputFloat3("Value", key.value.data());
                    else m_dirty |= ImGui::InputFloat("Value", key.value.data());
                    if (ImGui::Button("Capture Current Value")) m_dirty |= scene::ReadTimelineValue(scene, track, key.value);
                }
                if (ImGui::Button("Duplicate Key at Playhead"))
                {
                    auto copy = key; copy.time = std::min(m_draft.duration, m_frame / Fps);
                    const int inserted = InsertTimelineKey(track, copy, m_draft.duration);
                    if (inserted >= 0) { m_key = inserted; m_dirty = true; }
                    else m_error = "Choose an unoccupied frame for the duplicate key.";
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete Key")) { track.keys.erase(track.keys.begin() + m_key); m_key = -1; m_dirty = true; }
            }
        }
        if (!sequence->MissingBindings().empty()) ImGui::TextWrapped("Some tracks have a missing target or required component.");
        if (m_dirty && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (m_draft.Validate(&m_error))
            {
                preview.Stop();
                editor.ExecuteSceneEdit("Edit sequence", [&]()
                {
                    sequence->SetTimeline(m_draft);
                    owner->AddPrefabOverride("Component:SequencerComponent:Timeline");
                });
                m_source = sequence->GetTimeline().Encode(); m_dirty = false;
                preview.Begin(scene, m_owner, false); preview.Seek(std::min(m_draft.duration, m_frame / Fps));
            }
        }
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
        if (m_dirty && ImGui::Button("Discard Pending Edits")) { m_draft = sequence->GetTimeline(); m_dirty = false; m_error.clear(); }
    }
}
