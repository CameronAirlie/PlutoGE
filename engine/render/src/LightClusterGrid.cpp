#include "LightClusterGrid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace PlutoGE::render
{
    namespace
    {
        // Spheres grow by this much (relative and absolute, in world units) so a surface point that rounds into a
        // neighbouring cluster still finds every light whose range contains it.
        constexpr float RelativeInflation = 1.0e-3f;
        constexpr float AbsoluteInflation = 1.0e-3f;
        // The first slice ends no closer than this; everything nearer shares slice zero.
        constexpr float MinimumFirstSliceDepth = 0.05f;

        bool Rigid(const glm::mat4 &view)
        {
            if (std::abs(view[0][3]) > 1.0e-6f || std::abs(view[1][3]) > 1.0e-6f ||
                std::abs(view[2][3]) > 1.0e-6f || std::abs(view[3][3] - 1.0f) > 1.0e-6f)
                return false;
            const glm::mat3 rotation(view);
            const glm::mat3 product = glm::transpose(rotation) * rotation;
            for (int column = 0; column < 3; ++column)
                for (int row = 0; row < 3; ++row)
                    if (!(std::abs(product[column][row] - (column == row ? 1.0f : 0.0f)) < 1.0e-3f))
                        return false;
            return true;
        }

        template <class T>
        auto Bytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }
    }

    void LightClusterGrid::Initialize(rhi::IRenderDevice &device, const rhi::ComputePipelineDescriptor::ShaderCode &culling)
    {
        using namespace rhi;
        m_masks = Buffer(device, device.CreateBuffer(
            {PLUTO_LIGHT_CLUSTER_COUNT * PLUTO_LIGHT_CLUSTER_WORDS * sizeof(std::uint32_t), BufferUsage::Storage,
             "Light cluster masks"}));
        if (culling.glsl.empty() && culling.spirv.empty())
            return;
        m_pipeline = GraphicsPipeline(device, device.CreateComputePipeline(
            {culling,
             {{0, 0, 0, ResourceBindingType::UniformBuffer, ShaderStageMask::Compute},
              {1, 0, 1, ResourceBindingType::StorageBuffer, ShaderStageMask::Compute}},
             "Light cluster culling"}));
        if (m_pipeline)
            m_parameters = Buffer(device, device.CreateBuffer(
                {sizeof(CullingParameters), BufferUsage::Uniform, "Light cluster culling parameters"}));
    }

    LightClusterGrid::Fitting LightClusterGrid::Fit(const glm::mat4 &view, const glm::mat4 &projection,
                                                    std::span<const LocalLightBounds> lights,
                                                    std::span<glm::vec4> cullingSpheres)
    {
        if (lights.empty() || lights.size() > PLUTO_MAX_LOCAL_LIGHTS || !Rigid(view))
            return {};
        float nearest = std::numeric_limits<float>::max();
        float farthest = std::numeric_limits<float>::lowest();
        for (std::size_t index = 0; index < lights.size(); ++index)
        {
            const auto &light = lights[index];
            // The shader skips lights without positive range, so an empty sphere (never matched) is exact.
            glm::vec4 sphere{0.0f, 0.0f, 0.0f, -1.0f};
            if (light.range > 0.0f && std::isfinite(light.range) && std::isfinite(light.position.x) &&
                std::isfinite(light.position.y) && std::isfinite(light.position.z))
            {
                const glm::vec3 centre = glm::vec3(view * glm::vec4(light.position, 1.0f));
                const float radius = light.range * (1.0f + RelativeInflation) + AbsoluteInflation;
                sphere = glm::vec4(centre, radius);
                nearest = std::min(nearest, -centre.z - radius);
                farthest = std::max(farthest, -centre.z + radius);
            }
            if (index < cullingSpheres.size())
                cullingSpheres[index] = sphere;
        }
        if (nearest > farthest)
            return {};
        // Perspective views never shade points behind the eye; orthographic ones can.
        const bool perspective = projection[3][3] == 0.0f;
        const float firstSlice = std::max(nearest, MinimumFirstSliceDepth);
        const float sliceFar = std::max(farthest, firstSlice * 2.0f);
        Fitting fitting;
        fitting.shading.projection = projection;
        fitting.shading.depth = {firstSlice, PLUTO_LIGHT_CLUSTER_Z / std::log(sliceFar / firstSlice), farthest, 1.0f};
        fitting.sliceZeroDepth = perspective ? 0.0f : std::min(0.0f, nearest);
        return fitting;
    }

    LightClusterShading LightClusterGrid::Prepare(rhi::IRenderDevice &device, const glm::mat4 &view,
                                                  const glm::mat4 &projection, std::span<const LocalLightBounds> lights)
    {
        m_pending = false;
        if (!Available())
            return {};
        CullingParameters parameters{};
        const auto fitting = Fit(view, projection, lights,
                                 std::span(parameters.lights).first(std::min(lights.size(), parameters.lights.size())));
        if (!fitting.shading.Enabled())
            return fitting.shading;
        parameters.inverseProjection = glm::inverse(projection);
        parameters.depth = glm::vec4(glm::vec3(fitting.shading.depth), fitting.sliceZeroDepth);
        parameters.counts.x = static_cast<std::uint32_t>(lights.size());
        // The masks persist and only this grid writes them: an unchanged view and light set needs no new culling.
        if (m_recorded && std::memcmp(&parameters, &m_recordedParameters, sizeof(parameters)) == 0)
            return fitting.shading;
        device.UpdateBuffer(m_parameters.Get(), 0, Bytes(parameters));
        m_preparedParameters = parameters;
        m_pending = true;
        return fitting.shading;
    }

    bool LightClusterGrid::Record(rhi::ICommandContext &commands)
    {
        if (!m_pending)
            return false;
        m_pending = false;
        m_recordedParameters = m_preparedParameters;
        m_recorded = true;
        using rhi::BarrierAccess;
        // Earlier views' shading may still read the masks this overwrites.
        commands.PipelineBarrier(BarrierAccess::FragmentShaderRead, BarrierAccess::ComputeShaderWrite);
        commands.BindPipeline(m_pipeline.Get());
        commands.BindUniformBuffer(0, m_parameters.Get());
        commands.BindStorageBuffer(1, m_masks.Get());
        commands.Dispatch((PLUTO_LIGHT_CLUSTER_COUNT + PLUTO_LIGHT_CLUSTER_GROUP_SIZE - 1) / PLUTO_LIGHT_CLUSTER_GROUP_SIZE, 1, 1);
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite, BarrierAccess::FragmentShaderRead);
        return true;
    }
}
