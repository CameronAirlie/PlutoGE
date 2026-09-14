#pragma once
#include "PlutoGE/scene/TimelineBindings.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/SequencerComponent.h"
#include <memory>

namespace PlutoGE::ui
{
    // Persistent state is IDs, revisions and immutable timeline data, never scene
    // pointers. EditorShell resets this session whenever it replaces a scene.
    class TimelinePreview
    {
    public:
        bool Begin(scene::Scene &scene, scene::EntityID owner, bool playing)
        {
            auto *entity = scene.FindEntityByID(owner);
            auto *sequence = entity ? entity->GetComponent<scene::SequencerComponent>() : nullptr;
            if (scene.IsRuntimeStarted() || !sequence || !m_player.Start(sequence->GetTimeline())) return false;
            m_owner = owner;
            m_componentRevision = entity->GetComponentRevision();
            m_dataRevision = sequence->GetTimelineRevision();
            m_playing = playing;
            return true;
        }
        void Stop() { m_owner = 0; m_playing = false; m_player.Stop(); }
        bool Seek(double time) { m_playing = false; return m_owner && m_player.Seek(time); }
        scene::EntityID Owner() const { return m_owner; }
        double Time() const { return m_player.Time(); }
        bool IsPlaying() const { return m_playing; }
        std::unique_ptr<scene::ScopedTimelinePose> RenderPose(scene::Scene &scene, double delta)
        {
            auto *entity = scene.FindEntityByID(m_owner);
            auto *sequence = entity ? entity->GetComponent<scene::SequencerComponent>() : nullptr;
            if (!m_owner || scene.IsRuntimeStarted() || !sequence || !sequence->IsEnabled() || !entity->IsActive() ||
                entity->GetComponentRevision() != m_componentRevision || sequence->GetTimelineRevision() != m_dataRevision)
            { Stop(); return {}; }
            if (m_playing)
            {
                std::vector<scene::TimelineEvent> ignored;
                if (!m_player.Advance(delta, ignored)) { Stop(); return {}; }
                m_playing = m_player.IsPlaying();
            }
            return std::make_unique<scene::ScopedTimelinePose>(scene, m_player);
        }
    private:
        scene::EntityID m_owner = 0;
        std::uint64_t m_componentRevision = 0, m_dataRevision = 0;
        scene::TimelinePlayer m_player;
        bool m_playing = false;
    };
}
