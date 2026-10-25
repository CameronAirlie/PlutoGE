#pragma once
#include "PlutoGE/render/ShadowFaceCache.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    // Stationary/moving split for point shadow faces (cf. CascadedShadowLayers).
    // One moving caster, such as a sweeping security camera, used to redraw
    // every caster of each face it touched. Stationary casters are instead kept
    // in a static atlas face and copied in; only moving casters are redrawn.
    // Stability is observed from content signatures, never an authoring flag.
    class PointShadowLayers
    {
        // Keyed by content signature, not draw index: draws shift position
        // whenever anything earlier in the list appears or goes, which would
        // otherwise demote every caster after it.
        std::unordered_map<std::uint64_t, unsigned> m_stableFrames, m_nextStableFrames;
        std::vector<unsigned> m_casterFrames;
      public:
        static constexpr std::size_t FaceCount = 24;
        // A caster joins the static layer only after this many unchanged frames,
        // and leaves it on its first change. Each change of membership rebuilds
        // the static faces it touches, so a guard pausing for a moment between
        // steps must not be promoted.
        static constexpr unsigned StationaryFrames = 30;
        // Static content per atlas face; a key is published once that face of
        // the static atlas has been cleared and recorded.
        std::array<ShadowFaceCache, FaceCount> faces;

        // Call once per frame with every caster's signature, indexed like the draws.
        void Observe(std::span<const std::uint64_t> signatures)
        {
            m_nextStableFrames.clear();
            m_casterFrames.resize(signatures.size());
            for (std::size_t i = 0; i < signatures.size(); ++i)
            {
                const auto previous = m_stableFrames.find(signatures[i]);
                const unsigned frames = previous == m_stableFrames.end() ? 0 : std::min(previous->second + 1, StationaryFrames);
                m_nextStableFrames.insert_or_assign(signatures[i], frames);
                m_casterFrames[i] = frames;
            }
            std::swap(m_stableFrames, m_nextStableFrames);
        }

        [[nodiscard]] bool IsStationary(std::size_t caster) const
        {
            return caster < m_casterFrames.size() && m_casterFrames[caster] >= StationaryFrames;
        }

        // Splits a face's casters; false when the split is not worth a static
        // copy (the caller then redraws the face whole). Seeding a 512 texel face
        // costs far less than redrawing a few dozen casters, so the bar is low.
        bool Partition(std::span<const std::size_t> casters, std::vector<std::size_t> &stationary,
                       std::vector<std::size_t> &moving) const
        {
            stationary.clear();
            moving.clear();
            for (const auto caster : casters)
                (IsStationary(caster) ? stationary : moving).push_back(caster);
            return stationary.size() >= 16 && stationary.size() >= moving.size() * 4;
        }

        void Invalidate()
        {
            for (auto &face : faces) face.Reset();
        }
    };
}
