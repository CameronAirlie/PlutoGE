#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    // Temporal reconstruction of the volumetric cloud trace (CloudTemporal.slang).
    //
    // A cloud trace has few steps for the distance it covers. With the same
    // step offsets in every pixel, each step's sample shell shows as a thin
    // slice through the clouds. With history available the trace instead
    // jitters its offsets per pixel and frame (VolumetricCloud.slang
    // cloudStepOffset), and this filter reprojects and accumulates the result,
    // turning the slices into a converged image at the same per-frame cost.
    //
    // Each cloud volume in a frame keeps its own history, addressed by its
    // order among the frame's cloud packets. Between BeginFrame and EndFrame,
    // ask JitterTrace before tracing a volume and Resolve after.
    class CloudTemporalFilter
    {
    public:
        using ShaderCode = rhi::GraphicsPipelineDescriptor::ShaderCode;
        struct Shaders
        {
            ShaderCode vertex, fragment;
        };

        // The camera of the current frame, as the trace used it.
        struct Camera
        {
            glm::mat4 view{1.0f}, projection{1.0f};
            // Previous frame's unjittered view-projection (BasicLit motion's).
            glm::mat4 previousViewProjection{1.0f};
            glm::vec3 position{0.0f};
            bool flipY = false;
        };

        // Weight of the current trace in the accumulated result. Lower is
        // smoother and slower to follow lighting changes.
        static constexpr float CurrentWeight = 0.1f;

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        void Shutdown();
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }

        // continuous is false on the first frame or after a camera cut: every history restarts.
        void BeginFrame(const Camera &camera, bool continuous);
        // Whether volume's trace should jitter: its history will be reprojected this frame.
        [[nodiscard]] bool JitterTrace(std::size_t volume, std::uint32_t width, std::uint32_t height) const;
        // Accumulates the trace (rgb radiance, a transmittance) into volume's history and returns the result,
        // valid until the same volume's next Resolve. Call outside rendering.
        [[nodiscard]] rhi::TextureHandle Resolve(rhi::ICommandContext &commands, std::size_t volume,
                                                 rhi::TextureHandle trace, rhi::SamplerHandle sampler,
                                                 std::uint32_t width, std::uint32_t height,
                                                 const glm::mat4 &worldToLocal);
        // volumes is the number of cloud volumes resolved this frame; histories beyond it are released.
        void EndFrame(std::size_t volumes);

    private:
        struct History
        {
            rhi::Texture targets[2];
            rhi::Buffer parameters;
            std::uint32_t width = 0, height = 0;
            std::uint32_t current = 0;
            bool valid = false;
            bool resolvedThisFrame = false;
        };

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        std::vector<History> m_histories;
        Camera m_camera;
        bool m_continuous = false;
    };
}
