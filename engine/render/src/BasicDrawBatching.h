#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <bit>
#include <type_traits>
#include <unordered_map>

namespace PlutoGE::render
{
    // Batch only after per-object visibility/LOD selection. Shadow and GI source
    // lists retain their original bounds and identities for cache validation.
    inline bool SameBasicDrawMaterial(const BasicDraw &a, const BasicDraw &b)
    {
        return a.mesh == b.mesh
            && a.firstIndex == b.firstIndex
            && a.indexCount == b.indexCount
            && a.normalizedLod == b.normalizedLod
            && a.baseColor == b.baseColor
            && a.uvScale == b.uvScale
            && a.baseColorTexture == b.baseColorTexture
            && a.normalTexture == b.normalTexture
            && a.metallicTexture == b.metallicTexture
            && a.roughnessTexture == b.roughnessTexture
            && a.metallic == b.metallic
            && a.roughness == b.roughness
            && a.emission == b.emission
            && a.subsurface == b.subsurface
            && a.subsurfaceColor == b.subsurfaceColor
            && a.subsurfaceRadius == b.subsurfaceRadius
            && a.surfaceType == b.surfaceType
            && a.transmission == b.transmission
            && a.ior == b.ior
            && a.thickness == b.thickness
            && a.attenuationColor == b.attenuationColor
            && a.attenuationDistance == b.attenuationDistance
            && a.twoSided == b.twoSided
            && a.alphaCutoff == b.alphaCutoff
            && a.alphaMode == b.alphaMode
            && a.metallicChannel == b.metallicChannel
            && a.roughnessChannel == b.roughnessChannel
            && a.flipNormalY == b.flipNormalY
            && a.castsShadow == b.castsShadow
            && a.contributesToGi == b.contributesToGi;
    }

    template<class T> inline void HashBatchValue(std::size_t &hash, const T &value)
    {
        // Avoid running the library's byte-string hash for every scalar of
        // every draw. Normalize signed zero to match material equality.
        std::size_t bits;
        if constexpr (std::is_pointer_v<T>) bits = reinterpret_cast<std::size_t>(value);
        else if constexpr (std::is_same_v<T, float>) bits = value == 0.0f ? 0u : std::bit_cast<std::uint32_t>(value);
        else bits = static_cast<std::size_t>(value);
        hash ^= bits + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    }
    template<glm::length_t N, class T, glm::qualifier Q>
    inline void HashBatchValue(std::size_t &hash, const glm::vec<N, T, Q> &value)
    {
        for (glm::length_t i = 0; i < N; ++i) HashBatchValue(hash, value[i]);
    }
    template<class T> inline void HashBatchValue(std::size_t &hash, rhi::Handle<T> value)
    {
        HashBatchValue(hash, value.index);
        HashBatchValue(hash, value.generation);
    }

    inline void BatchOpaqueDraws(std::vector<BasicDraw> &draws)
    {
        struct Group
        {
            std::size_t index;
            std::shared_ptr<std::vector<glm::mat4>> models, previous;
        };
        std::unordered_map<std::size_t, std::vector<Group>> groups;
        std::size_t output = 0;
        for (std::size_t input = 0; input < draws.size(); ++input)
        {
            auto &draw = draws[input];
            // Existing instances, skinned sources and order-dependent surfaces
            // keep their original packets. Explicit history is required: draw
            // index history would be invalid after compaction.
            if (draw.mesh && draw.surfaceType == 0 && draw.alphaMode != 2 &&
                draw.contributesToGi && !draw.instanceModels && draw.previousModel)
            {
                std::size_t hash = 0;
                HashBatchValue(hash, draw.mesh);
                HashBatchValue(hash, draw.firstIndex);
                HashBatchValue(hash, draw.indexCount);
                HashBatchValue(hash, draw.normalizedLod);
                HashBatchValue(hash, draw.baseColor);
                HashBatchValue(hash, draw.uvScale);
                HashBatchValue(hash, draw.baseColorTexture);
                HashBatchValue(hash, draw.normalTexture);
                HashBatchValue(hash, draw.metallicTexture);
                HashBatchValue(hash, draw.roughnessTexture);
                HashBatchValue(hash, draw.metallic);
                HashBatchValue(hash, draw.roughness);
                HashBatchValue(hash, draw.emission);
                HashBatchValue(hash, draw.subsurface);
                HashBatchValue(hash, draw.subsurfaceColor);
                HashBatchValue(hash, draw.subsurfaceRadius);
                HashBatchValue(hash, draw.surfaceType);
                HashBatchValue(hash, draw.transmission);
                HashBatchValue(hash, draw.ior);
                HashBatchValue(hash, draw.thickness);
                HashBatchValue(hash, draw.attenuationColor);
                HashBatchValue(hash, draw.attenuationDistance);
                HashBatchValue(hash, draw.twoSided);
                HashBatchValue(hash, draw.alphaCutoff);
                HashBatchValue(hash, draw.alphaMode);
                HashBatchValue(hash, draw.metallicChannel);
                HashBatchValue(hash, draw.roughnessChannel);
                HashBatchValue(hash, draw.flipNormalY);
                HashBatchValue(hash, draw.castsShadow);
                HashBatchValue(hash, draw.contributesToGi);
                auto &candidates = groups[hash];
                auto found = std::find_if(candidates.begin(), candidates.end(), [&](const Group &g)
                    { return SameBasicDrawMaterial(draws[g.index], draw); });
                if (found != candidates.end())
                {
                    auto &first = draws[found->index];
                    if (!found->models)
                    {
                        found->models = std::make_shared<std::vector<glm::mat4>>();
                        found->previous = std::make_shared<std::vector<glm::mat4>>();
                        found->models->reserve(16);
                        found->previous->reserve(16);
                        found->models->push_back(first.model);
                        found->previous->push_back(*first.previousModel);
                        first.instanceModels = found->models;
                        first.previousInstanceModels = found->previous;
                    }
                    found->models->push_back(draw.model);
                    found->previous->push_back(*draw.previousModel);
                    if (first.shadowBoundsRadius >= 0 && draw.shadowBoundsRadius >= 0)
                        first.shadowBoundsRadius = std::max(first.shadowBoundsRadius,
                            glm::length(draw.shadowBoundsCenter - first.shadowBoundsCenter) + draw.shadowBoundsRadius);
                    else
                        first.shadowBoundsRadius = -1;
                    continue;
                }
                candidates.push_back({output, {}, {}});
            }
            if (input != output) draws[output] = std::move(draw);
            ++output;
        }
        draws.resize(output);
    }
}
