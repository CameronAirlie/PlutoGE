#include "InstanceBinning.h"

#include "PlutoGE/core/TaskPool.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    InstanceSphere InstanceBounds(const BasicInstanceLodChain &chain, const glm::mat4 &model)
    {
        const float scale = std::max({glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                                      glm::length(glm::vec3(model[2]))});
        return {glm::vec3(model * glm::vec4(chain.boundsCenter, 1.0f)), chain.boundsRadius * scale};
    }

    float ProjectedRadius(const InstanceSphere &sphere, const glm::vec3 &camera, float lodProjection)
    {
        const glm::vec3 offset = sphere.center - camera;
        return sphere.radius / std::sqrt(std::max(glm::dot(offset, offset), 0.000001f)) * lodProjection;
    }

    InstanceView::InstanceView(const glm::mat4 &viewProjection, const glm::vec3 &camera, float projectionScale,
                               float height, float detail)
        : frustum(viewProjection), camera(camera),
          lodProjection(std::abs(projectionScale) * 0.5f * height *
                        (std::isfinite(detail) ? std::clamp(detail, MinFoliageDetail, MaxFoliageDetail) : 1.0f))
    {
    }

    bool InstanceView::ClusterVisible(const BasicDraw &draw) const
    {
        // Unknown or deforming bounds: only the per-instance tests apply.
        if (draw.shadowBoundsRadius < 0.0f || !std::isfinite(draw.shadowBoundsRadius))
            return true;
        const glm::vec3 offset = draw.shadowBoundsCenter - camera;
        const float reach = draw.instanceDrawDistance + draw.shadowBoundsRadius;
        return glm::dot(offset, offset) <= reach * reach &&
               frustum.SidesIntersect(draw.shadowBoundsCenter, draw.shadowBoundsRadius);
    }

    std::size_t InstanceBinning::Bin(std::span<const Source> sources, const InstanceView &view, InstanceStorage &storage)
    {
        if (m_bins.size() < sources.size()) m_bins.resize(sources.size());
        m_culled.assign(sources.size(), 0);
        core::TaskPool::Shared().RunRanges(sources.size(), 16, [&](std::size_t, std::size_t begin, std::size_t end)
        {
            for (std::size_t item = begin; item < end; ++item)
            {
                const auto &draw = *sources[item].draw;
                const auto &chain = *draw.instanceLods;
                auto &bins = m_bins[item];
                bins.resize(chain.lods.size());
                for (auto &bin : bins) bin.clear();
                const auto &models = *draw.instanceModels;
                for (std::uint32_t instance = 0; instance < models.size(); ++instance)
                {
                    const auto sphere = InstanceBounds(chain, models[instance]);
                    const glm::vec3 offset = sphere.center - view.camera;
                    const float reach = draw.instanceDrawDistance + sphere.radius;
                    if (glm::dot(offset, offset) > reach * reach || !view.frustum.SidesIntersect(sphere.center, sphere.radius))
                    {
                        ++m_culled[item];
                        continue;
                    }
                    // Instances another draw covers (a mesh's impostor-sized ones) go in no bin of this draw.
                    if (const auto lod = chain.Select(ProjectedRadius(sphere, view.camera, view.lodProjection));
                        lod != chain.NotDrawn())
                        bins[lod].push_back(instance);
                }
            }
        });
        std::size_t culled = 0;
        for (std::size_t item = 0; item < sources.size(); ++item)
        {
            const auto &draw = *sources[item].draw;
            // StageBins reads only this draw's LODs: a reused entry may hold more from a longer chain.
            m_bins[item].resize(draw.instanceLods->lods.size());
            storage.StageBins(sources[item].records, draw.instanceModels.get(), draw.instanceLods.get(), m_bins[item]);
            culled += m_culled[item];
        }
        return culled;
    }
}
