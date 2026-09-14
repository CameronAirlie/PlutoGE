#pragma once
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/Timeline.h"
#include <unordered_map>

namespace PlutoGE::scene
{
    class SequencerComponent : public TypedComponent<SequencerComponent>
    {
    public:
        void Update(float deltaTime) override;
        bool SetTimeline(const Timeline &timeline);
        const Timeline &GetTimeline() const { return m_timeline; }
        std::uint64_t GetTimelineRevision() const { return m_timelineRevision; }
        bool Play();
        void Stop();
        bool Scrub(double seconds);
        bool IsPlaying() const { return m_player.IsPlaying(); }
        double GetTime() const { return m_player.Time(); }
        bool GetPlayOnStart() const { return m_playOnStart; }
        void ResetRuntime();
        std::vector<std::size_t> MissingBindings() const;
        void RemapBindings(const std::unordered_map<std::uint32_t, std::uint32_t> &ids);
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;
    private:
        void Apply();
        Timeline m_timeline;
        std::uint64_t m_timelineRevision = 0;
        TimelinePlayer m_player;
        bool m_playOnStart = true, m_started = false;
    };
}
