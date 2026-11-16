#include "PlutoGE/scene/water/WaterInteraction.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene::water
{
    const char *ToString(WaterInteractionType type)
    {
        switch (type)
        {
        case WaterInteractionType::Exit: return "Exit";
        case WaterInteractionType::Wake: return "Wake";
        case WaterInteractionType::Enter: break;
        }
        return "Enter";
    }

    float WaterInteractionTracker::Intensity(const WaterProbe &probe, float speed) const
    {
        const float speedFactor = speed / std::max(m_settings.referenceSpeed, 1e-3f);
        const float sizeFactor = std::sqrt(std::max(probe.radius, 0.0f) / std::max(m_settings.referenceRadius, 1e-3f));
        return std::clamp(speedFactor * sizeFactor * std::max(probe.effectScale, 0.0f), 0.0f, 4.0f);
    }

    bool WaterInteractionTracker::IsTouchingWater(std::uint32_t entity) const
    {
        const auto found = m_states.find(entity);
        return found != m_states.end() && found->second.touching;
    }

    void WaterInteractionTracker::Update(std::span<const WaterProbe> probes, const SurfaceSampler &sample, float deltaTime,
                                         std::vector<WaterInteractionEvent> &events)
    {
        ++m_frame;
        const float step = std::isfinite(deltaTime) ? std::max(deltaTime, 0.0f) : 0.0f;
        for (const auto &probe : probes)
        {
            if (!std::isfinite(probe.center.x) || !std::isfinite(probe.center.y) || !std::isfinite(probe.center.z))
                continue;
            const bool known = m_states.contains(probe.entity);
            auto &state = m_states[probe.entity];
            state.seen = m_frame;
            state.cooldown = std::max(state.cooldown - step, 0.0f);

            const auto surface = sample ? sample(probe.center) : std::nullopt;
            const float radius = std::max(probe.radius, 0.01f);
            const bool touching = surface && probe.center.y - radius < surface->height;
            const glm::vec3 velocity = probe.velocity - (surface ? surface->flow : glm::vec3(0.0f));
            if (!known || !surface)
            {
                // First sight, or off every water area (ashore): no splash, start afresh.
                state.touching = touching;
                state.water = surface ? surface->entity : 0;
                state.wakeDistance = 0.0f;
                continue;
            }

            WaterInteractionEvent event;
            event.entity = probe.entity;
            event.waterEntity = surface->entity;
            event.waterKind = surface->kind;
            event.position = {probe.center.x, surface->height, probe.center.z};
            event.velocity = velocity;
            event.radius = radius;
            event.mass = probe.mass;
            event.channels = probe.channels;

            if (touching && !state.touching)
            {
                const float down = -velocity.y;
                if (down >= m_settings.enterSpeed && state.cooldown <= 0.0f)
                {
                    event.type = WaterInteractionType::Enter;
                    event.intensity = Intensity(probe, glm::length(velocity));
                    events.push_back(event);
                    state.cooldown = m_settings.splashCooldown;
                }
                state.wakeDistance = 0.0f;
            }
            else if (!touching && state.touching)
            {
                if (velocity.y >= m_settings.exitSpeed && state.cooldown <= 0.0f)
                {
                    event.type = WaterInteractionType::Exit;
                    event.intensity = Intensity(probe, velocity.y) * 0.6f;
                    events.push_back(event);
                    state.cooldown = m_settings.splashCooldown;
                }
            }
            else if (touching && probe.center.y + radius > surface->height)
            {
                // Crossing the surface: a wake every few tens of centimetres travelled along it.
                const float along = glm::length(glm::vec2(velocity.x, velocity.z));
                if (along >= m_settings.wakeSpeed)
                {
                    state.wakeDistance += along * step;
                    const float spacing = std::max(m_settings.wakeSpacing, radius * 0.75f);
                    if (state.wakeDistance >= spacing)
                    {
                        state.wakeDistance = std::fmod(state.wakeDistance, spacing);
                        event.type = WaterInteractionType::Wake;
                        event.intensity = Intensity(probe, along) * 0.5f;
                        events.push_back(event);
                    }
                }
                else
                    state.wakeDistance = 0.0f;
            }
            state.touching = touching;
            state.water = surface->entity;
        }
        std::erase_if(m_states, [&](const auto &entry) { return entry.second.seen != m_frame; });
    }
}
