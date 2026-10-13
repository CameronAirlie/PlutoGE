#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <functional>
#include <tuple>

namespace PlutoGE::render
{
    // Batch after cascade culling: merging never expands caster visibility.
    // Opaque coverage depends on geometry, not surface material. Masks and
    // shader-graph vertex offsets retain the general shadow pipeline.
    class OpaqueShadowBatches
    {
      public:
        static constexpr std::size_t Capacity = 64;
        struct Batch
        {
            const BasicMesh *mesh = nullptr;
            std::uint32_t firstIndex = 0, indexCount = 0, count = 0;
            std::array<glm::mat4, Capacity> models{};
        };
        static bool Eligible(const BasicDraw &draw)
        {
            return draw.alphaMode == 0 && draw.surfaceType == 0 &&
                (!draw.shaderGraphProgram || draw.shaderGraphProgram->data.header.z == 0);
        }
        void Build(std::span<const BasicDraw> draws, std::span<const std::size_t> visible)
        {
            m_indices.clear(); batches.clear(); fallback.clear();
            for (const auto index : visible)
                (Eligible(draws[index]) ? m_indices : fallback).push_back(index);
            std::sort(m_indices.begin(), m_indices.end(), [&](auto a, auto b) {
                const auto &left = draws[a], &right = draws[b];
                if (left.mesh != right.mesh) return std::less<const BasicMesh *>{}(left.mesh, right.mesh);
                return std::tie(left.firstIndex, left.indexCount) < std::tie(right.firstIndex, right.indexCount);
            });
            for (const auto index : m_indices)
            {
                const auto &draw = draws[index];
                const auto append = [&](const glm::mat4 &model) {
                    if (batches.empty() || batches.back().mesh != draw.mesh ||
                        batches.back().firstIndex != draw.firstIndex || batches.back().indexCount != draw.indexCount ||
                        batches.back().count == Capacity)
                    {
                        batches.emplace_back();
                        auto &batch = batches.back();
                        batch.mesh = draw.mesh; batch.firstIndex = draw.firstIndex; batch.indexCount = draw.indexCount;
                    }
                    auto &batch = batches.back();
                    batch.models[batch.count++] = model;
                };
                // Preserve the existing shadow path's singleton semantics.
                if (draw.instanceModels && draw.instanceModels->size() > 1)
                    for (const auto &model : *draw.instanceModels) append(model);
                else append(draw.model);
            }
        }
        std::vector<Batch> batches;
        std::vector<std::size_t> fallback;
      private:
        std::vector<std::size_t> m_indices;
    };
}
