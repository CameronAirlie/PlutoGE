#pragma once

#include "ClipFrustum.h"
#include "InstanceStorage.h"
#include "PlutoGE/render/BasicRenderer.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // An instance's world bounding sphere: its LOD chain's mesh sphere under the instance's largest axis scale.
    struct InstanceSphere
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };
    [[nodiscard]] InstanceSphere InstanceBounds(const BasicInstanceLodChain &chain, const glm::mat4 &model);

    // The sphere's projected radius in pixels. lodProjection is the view's InstanceView::lodProjection, so a radius over
    // a distance gives pixels.
    [[nodiscard]] float ProjectedRadius(const InstanceSphere &sphere, const glm::vec3 &camera, float lodProjection);

    // Whether a draw's instances are culled and binned by LOD each frame (on the CPU here, or by GpuInstanceCulling):
    // more than one instance and a LOD chain.
    [[nodiscard]] inline bool IsBinnedInstanceDraw(const BasicDraw &draw) noexcept
    {
        return draw.instanceLods && draw.instanceModels && draw.instanceModels->size() > 1;
    }

    // The camera binned draws are culled and binned for. CPU binning, GPU binning (GpuInstanceCulling) and the camera
    // passes take it from one place, so they agree on which draws and LODs each instance falls in.
    struct InstanceView
    {
        // `projectionScale` is the projection's y scale and `height` the render height in pixels. `detail` scales
        // every instance's projected size before LOD selection (BasicLighting::foliageDetail): below 1, instances
        // take coarser LODs, and hand over to their impostors, from nearer.
        InstanceView(const glm::mat4 &viewProjection, const glm::vec3 &camera, float projectionScale, float height,
                     float detail);

        ClipFrustum frustum;
        glm::vec3 camera{0.0f};
        // Pixels per unit of radius over distance, with the detail scale applied.
        float lodProjection = 0.0f;

        // Whether any instance of a binned draw can draw: its cluster sphere (BasicDraw::shadowBounds) meets the
        // view's sides and lies within its instance draw distance. Binning skips the others, and the camera passes
        // skip them before binding anything: a scene's out-of-view foliage cells cost nothing past this test.
        [[nodiscard]] bool ClusterVisible(const BasicDraw &draw) const;
    };

    // Bins binned draws' instances by LOD on the CPU: per instance, a view and draw-distance test, then
    // BasicInstanceLodChain::Select on its projected radius. The bins are staged in `storage` for the camera passes
    // (InstanceStorage::FindBins). Runs in parallel over draws.
    class InstanceBinning
    {
    public:
        // One draw to bin, whose records were prepared this frame.
        struct Source
        {
            const BasicDraw *draw = nullptr;
            InstanceStorage::Range records;
        };

        // Bins and stages `sources`, returning how many of their instances were outside the view or beyond their
        // draw distance.
        std::size_t Bin(std::span<const Source> sources, const InstanceView &view, InstanceStorage &storage);

    private:
        // Per source and LOD, the instances binned (kept between frames so their capacity is reused).
        std::vector<std::vector<std::vector<std::uint32_t>>> m_bins;
        std::vector<std::size_t> m_culled;
    };
}
