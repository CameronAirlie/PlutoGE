#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    // The impostor card draw of a mesh draw that spawns one (BasicDraw::spawnsImpostor): the same instances, covering
    // the projected sizes the mesh's chain hands over (below the impostor's maxScreenRadiusPixels, measured on the mesh
    // chain's bounding sphere so that every instance is drawn by exactly one of them), shaded with the source's
    // material constants (roughness, subsurface) and the impostor atlases as its base colour and normal maps.
    BasicDraw MakeImpostorDraw(const BasicDraw &source);

    // Expands a frame's draws with their impostor card draws, after the mesh draws. Impostor packets persist per
    // source packet (BasicDraw::preparationRevision), so unchanged foliage keeps the same packets, chains and bins.
    class ImpostorDraws
    {
    public:
        // The draws followed by one impostor draw for each that spawns one; the draws themselves when none does.
        // The expansion is rebuilt only when `changed` (the caller rebuilt the draw list) or on first use.
        std::span<const BasicDraw> Expand(std::span<const BasicDraw> draws, bool changed,
                                          const std::function<std::uint64_t()> &nextRevision);
        // Impostor draws in the last expansion.
        [[nodiscard]] std::size_t Count() const noexcept { return m_count; }

    private:
        std::unordered_map<std::uint64_t, BasicDraw> m_bySource;
        std::vector<BasicDraw> m_expanded;
        std::size_t m_count = 0;
        bool m_built = false;
    };
}
