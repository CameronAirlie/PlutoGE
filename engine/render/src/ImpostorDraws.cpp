#include "ImpostorDraws.h"

#include "BasicDrawBatching.h"

#include <limits>
#include <memory>

namespace PlutoGE::render
{
    BasicDraw MakeImpostorDraw(const BasicDraw &source)
    {
        // Keep instances, history, distances and the material's constants; replace what the cards draw with.
        BasicDraw draw = source;
        const auto &impostor = *source.spawnsImpostor;
        draw.impostor = source.spawnsImpostor;
        draw.spawnsImpostor = nullptr;

        draw.baseColor = glm::vec4(1.0f);
        draw.uvScale = glm::vec2(1.0f);
        draw.baseColorTexture = impostor.albedo;
        draw.normalTexture = impostor.normalDepth;
        draw.flipNormalY = false;
        draw.metallicTexture = draw.roughnessTexture = draw.emissionTexture = {};
        draw.surfaceTextures = {};
        draw.graphTextures = {};
        draw.emission = glm::vec3(0.0f);
        draw.clearCoat = 0.0f;
        draw.sheenColor = glm::vec3(0.0f);
        draw.shaderGraphProgram = nullptr;
        draw.terrainLayers = nullptr;
        // Cards are distant stand-ins, smaller than the sway: they stay still (docs/FOLIAGE_WIND.md).
        draw.foliageWind = {};
        // The atlas alpha is the baked coverage.
        draw.alphaMode = 1;
        draw.alphaCutoff = 0.5f;
        draw.twoSided = true;
        draw.surfaceType = 0;
        draw.outlinePass = false;
        // Cards stand in for the camera passes only: shadows and GI keep the mesh.
        draw.castsShadow = false;
        draw.contributesToGi = false;
        draw.firstIndex = 0;
        draw.indexCount = 6;
        draw.occlusionBoundsExtents = glm::vec3(-1.0f);

        auto chain = std::make_shared<BasicInstanceLodChain>();
        chain->lods = {{0, 6, std::numeric_limits<float>::max()}};
        chain->boundsCenter = source.instanceLods->boundsCenter;
        chain->boundsRadius = source.instanceLods->boundsRadius;
        chain->maxScreenRadiusPixels = impostor.parameters.maxScreenRadiusPixels;
        draw.instanceLods = std::move(chain);
        draw.preparedMaterialHash = BasicMaterialBatchHash(draw);
        return draw;
    }

    std::span<const BasicDraw> ImpostorDraws::Expand(std::span<const BasicDraw> draws, bool changed,
                                                     const std::function<std::uint64_t()> &nextRevision)
    {
        if (m_built && !changed)
            return m_count == 0 ? draws : std::span<const BasicDraw>(m_expanded);
        m_built = true;
        std::unordered_map<std::uint64_t, BasicDraw> bySource;
        std::vector<BasicDraw> impostors;
        for (const auto &draw : draws)
        {
            if (!draw.spawnsImpostor || !draw.instanceLods || !draw.instanceModels) continue;
            // Retained sources keep their impostor packet; others get a fresh one each expansion.
            if (draw.preparationRevision != 0)
            {
                auto found = m_bySource.find(draw.preparationRevision);
                if (found == m_bySource.end())
                {
                    auto made = MakeImpostorDraw(draw);
                    made.preparationRevision = nextRevision();
                    found = m_bySource.emplace(draw.preparationRevision, std::move(made)).first;
                }
                impostors.push_back(found->second);
                bySource.insert(*found);
            }
            else
                impostors.push_back(MakeImpostorDraw(draw));
        }
        m_bySource = std::move(bySource);
        m_count = impostors.size();
        m_expanded.clear();
        if (m_count == 0) return draws;
        m_expanded.reserve(draws.size() + impostors.size());
        m_expanded.insert(m_expanded.end(), draws.begin(), draws.end());
        m_expanded.insert(m_expanded.end(), std::make_move_iterator(impostors.begin()), std::make_move_iterator(impostors.end()));
        return m_expanded;
    }
}
