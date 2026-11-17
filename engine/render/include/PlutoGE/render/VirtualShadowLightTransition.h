#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <optional>

namespace PlutoGE::render
{
    // The directions the two directional clipmaps render for in one frame (PLUTO_VSM_INCOMING_BASE).
    struct VirtualShadowLightTurn
    {
        // The direction of the presented clipmap, which receivers sample.
        glm::vec3 presented{0.0f, -1.0f, 0.0f};
        // While the light turns: the direction of the incoming clipmap, planned and rendered in the background.
        std::optional<glm::vec3> incoming;
        // The incoming clipmap became the presented one this frame: its pages move to the presented levels and the
        // previous presented pages are released.
        bool promote = false;
    };

    // Turns the directional clipmaps to a new light direction without dropping shadows.
    //
    // Every page of a clipmap is drawn for one light direction, so turning the light (a day/night cycle stepping the
    // sun) invalidates all of them at once; rendered under the per-frame page budget, the new pages would take several
    // frames, during which receivers show missing or coarse shadows. Instead the clipmap keeps presenting the old
    // direction while an incoming clipmap for the new one renders alongside it, and is replaced once the incoming
    // clipmap is resident. Shadows therefore trail a turning light by the frames that takes; the light's shading turns
    // at once, and a turn small enough to transition this way differs from the trailing shadows imperceptibly.
    //
    // CPU-only and fed with completed GPU observations, never waiting for them, so it can be tested directly.
    class VirtualShadowLightTransition
    {
    public:
        // Turns at least this large (a time skip, a teleport) cut over at once: trailing shadows would be plainly wrong.
        static constexpr float MaxTransitionDegrees = 15.0f;
        // A transition still incomplete after this many frames (the pool or budget cannot hold both clipmaps) is
        // presented anyway, so shadows never stay behind the light indefinitely.
        static constexpr std::uint32_t MaxTransitionFrames = 240;
        // Turns smaller than this keep the presented clipmap; float noise in an unmoving light must not re-render it.
        static constexpr float MinTurnCosine = 0.9999999f;

        // A completed GPU frame's report of the incoming clipmap.
        struct Observation
        {
            std::uint32_t frame = 0;
            // Pages the incoming clipmap requested that are not yet resident (PLUTO_VSM_STATS_INCOMING_PENDING).
            std::uint32_t incomingPending = 0;
        };

        // The clipmap directions for `frame`, given the light's direction. `cached` is false when nothing can be
        // presented while an incoming clipmap renders (directional virtual shadows off, or a cleared cache): the
        // clipmap then takes the light's direction at once.
        VirtualShadowLightTurn Advance(const glm::vec3 &lightDirection, std::uint32_t frame, bool cached,
                                       const std::optional<Observation> &observation)
        {
            const glm::vec3 target = Normalized(lightDirection);
            if (!cached || !m_presented)
            {
                Reset(target);
                return {target, std::nullopt, false};
            }
            VirtualShadowLightTurn turn;
            if (m_incoming)
            {
                const bool resident = observation && observation->frame >= m_started && observation->incomingPending == 0;
                if (resident || frame - m_started >= MaxTransitionFrames)
                {
                    m_presented = m_incoming;
                    m_incoming.reset();
                    turn.promote = true;
                }
                else if (!Transitions(*m_incoming, target))
                {
                    // The light jumped while turning: cut over rather than finish a transition toward a stale direction.
                    Reset(target);
                    return {target, std::nullopt, false};
                }
            }
            if (!m_incoming && glm::dot(*m_presented, target) < MinTurnCosine)
            {
                if (Transitions(*m_presented, target))
                {
                    m_incoming = target;
                    m_started = frame;
                }
                else if (!turn.promote) Reset(target);
                // A cut in a promoting frame waits a frame: this frame's pages move to the presented levels first.
            }
            turn.presented = *m_presented;
            turn.incoming = m_incoming;
            return turn;
        }
        [[nodiscard]] bool InProgress() const noexcept { return m_incoming.has_value(); }
        // The frame the current transition started, while one is in progress.
        [[nodiscard]] std::uint32_t StartFrame() const noexcept { return m_started; }

    private:
        static glm::vec3 Normalized(const glm::vec3 &direction)
        {
            const float lengthSquared = glm::dot(direction, direction);
            return std::isfinite(lengthSquared) && lengthSquared > 1.e-8f ? direction / std::sqrt(lengthSquared)
                                                                         : glm::vec3(0.0f, -1.0f, 0.0f);
        }
        static bool Transitions(const glm::vec3 &from, const glm::vec3 &to)
        {
            return glm::dot(from, to) >= std::cos(glm::radians(MaxTransitionDegrees));
        }
        void Reset(const glm::vec3 &direction)
        {
            m_presented = direction;
            m_incoming.reset();
        }
        std::optional<glm::vec3> m_presented, m_incoming;
        std::uint32_t m_started = 0;
    };
}
