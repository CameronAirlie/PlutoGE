#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // One billboard of an emitter draw, keyed by its view depth.
    struct ParticleInstanceKey
    {
        float depth = 0.0f;
        std::uint32_t draw = 0, instance = 0;
    };

    // Alpha-blended billboards from different emitters must blend in a single
    // back-to-front order, not emitter by emitter. Each emitter keeps its own
    // material and lighting parameters, so the merged order is split into a
    // new draw wherever the emitter changes; separated emitters stay one draw
    // each. Vertex-only (trail) draws follow in source order. Keys from one
    // emitter must already be back-to-front; ties keep emitter order.
    // Returns the number of draws written to output, whose extra capacity is
    // kept for reuse.
    inline std::size_t OrderParticleDraws(std::span<const BasicParticleDraw> emitters,
                                          std::vector<ParticleInstanceKey> &keys,
                                          std::vector<BasicParticleDraw> &output)
    {
        std::ranges::stable_sort(keys, [](const auto &a, const auto &b) { return a.depth > b.depth; });
        std::size_t count = 0;
        const auto begin = [&](const BasicParticleDraw &source) -> BasicParticleDraw &
        {
            if (count == output.size()) output.emplace_back();
            auto &draw = output[count++];
            draw.vertices.clear();
            draw.instances.clear();
            draw.parameters = source.parameters;
            draw.texture = source.texture;
            return draw;
        };
        std::uint32_t current = 0;
        BasicParticleDraw *run = nullptr;
        for (const auto &key : keys)
        {
            if (!run || key.draw != current)
            {
                current = key.draw;
                run = &begin(emitters[key.draw]);
            }
            run->instances.push_back(emitters[key.draw].instances[key.instance]);
        }
        for (const auto &emitter : emitters)
            if (!emitter.vertices.empty())
                begin(emitter).vertices = emitter.vertices;
        return count;
    }
}
