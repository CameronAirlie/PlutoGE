#include "PlutoGE/scene/TimelineBindings.h"
#include "PlutoGE/scene/components/SequencerComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scripting/ScriptRuntime.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/render/Camera.h"
#include <stdexcept>

namespace PlutoGE::scene
{
    namespace
    {
        bool Bound(Entity *entity, TimelineChannel channel)
        {
            if (!entity) return false;
            switch (channel)
            {
            case TimelineChannel::CameraFov: { auto *c = entity->GetComponent<CameraComponent>(); return c && c->GetCamera(); }
            case TimelineChannel::LightColor: case TimelineChannel::LightIntensity: return entity->HasComponent<LightComponent>();
            case TimelineChannel::AudioVolume: case TimelineChannel::AudioPlay: return entity->HasComponent<SoundEmitterComponent>();
            case TimelineChannel::ScriptEvent: return entity->HasComponent<ScriptComponent>();
            default: return true;
            }
        }
    }
    bool SequencerComponent::SetTimeline(const Timeline &timeline)
    {
        if (!timeline.Validate()) return false;
        Stop();
        m_timeline = timeline;
        ++m_timelineRevision;
        return true;
    }
    bool SequencerComponent::Play()
    {
        if (!GetOwner() || !GetOwner()->GetScene() || !m_player.Start(m_timeline)) return false;
        m_started = true;
        Apply();
        return true;
    }
    void SequencerComponent::Stop() { m_player.Stop(); }
    void SequencerComponent::ResetRuntime() { Stop(); m_started = false; }
    bool SequencerComponent::Scrub(double seconds)
    {
        TimelinePlayer player;
        if (!player.Start(m_timeline) || !player.Seek(seconds)) return false;
        player.Stop();
        m_player = std::move(player);
        Apply();
        return true;
    }
    void SequencerComponent::Apply()
    {
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (!scene) return;
        const auto &tracks = m_player.Data().tracks;
        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            const auto &track = tracks[i];
            auto *entity = scene->FindEntityByID(track.entity);
            if (track.keys.empty() || !Bound(entity, track.channel)) continue;
            WriteTimelineValue(*scene, track, m_player.Evaluate(i));
        }
    }
    void SequencerComponent::Update(float deltaTime)
    {
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (!scene || !scene->IsRuntimeStarted() || !IsEnabled() || !GetOwner()->IsActive()) return;
        if (!m_started) { m_started = true; if (m_playOnStart) Play(); }
        if (!m_player.IsPlaying()) return;
        std::vector<TimelineEvent> events;
        if (!m_player.Advance(deltaTime, events)) { Stop(); return; }
        Apply();
        // Copy event payloads before user script callbacks can change authoring data.
        struct Dispatch { std::uint32_t entity; TimelineChannel channel; TimelineKey key; };
        std::vector<Dispatch> dispatch;
        for (const auto &event : events)
        {
            const auto &track = m_player.Data().tracks[event.track];
            dispatch.push_back({track.entity, track.channel, track.keys[event.key]});
        }
        for (const auto &event : dispatch)
        {
            auto *entity = scene->FindEntityByID(event.entity);
            if (!entity || !entity->IsActive()) continue;
            if (event.channel == TimelineChannel::AudioPlay)
            {
                if (auto *sound = entity->GetComponent<SoundEmitterComponent>(); sound && sound->IsEnabled()) sound->Play(true);
            }
            else
            {
                render::AnimationClip::Event scriptEvent;
                scriptEvent.name = event.key.event;
                scriptEvent.floatParameter = event.key.value[0];
                for (auto *script : entity->GetComponents<ScriptComponent>())
                    if (script->IsEnabled()) script->OnAnimationEvent(scriptEvent);
            }
        }
    }
    std::vector<std::size_t> SequencerComponent::MissingBindings() const
    {
        std::vector<std::size_t> missing;
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        for (std::size_t i = 0; i < m_timeline.tracks.size(); ++i)
            if (!Bound(scene ? scene->FindEntityByID(m_timeline.tracks[i].entity) : nullptr, m_timeline.tracks[i].channel)) missing.push_back(i);
        return missing;
    }
    void SequencerComponent::RemapBindings(const std::unordered_map<std::uint32_t, std::uint32_t> &ids)
    {
        Stop();
        ++m_timelineRevision;
        for (auto &track : m_timeline.tracks) if (auto it = ids.find(track.entity); it != ids.end()) track.entity = it->second;
    }
    std::vector<Property> SequencerComponent::Serialize() const
    {
        return {{"Timeline", PropertyType::String, m_timeline.Encode()}, {"Play On Start", PropertyType::Bool, m_playOnStart ? "true" : "false"}};
    }
    void SequencerComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto data = m_timeline;
        auto play = m_playOnStart;
        for (const auto &property : properties)
        {
            if (property.name == "Timeline")
            {
                std::string error;
                if (!Timeline::Decode(property.value, data, &error))
                    throw std::invalid_argument("Invalid serialized timeline: " + error);
            }
            if (property.name == "Play On Start") play = property.value == "true" || property.value == "1";
        }
        if (SetTimeline(data)) m_playOnStart = play;
    }
}
