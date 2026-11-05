#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
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
    // ask PlanTrace before tracing a volume and Resolve after.
    //
    // While a volume's history reprojects, its trace is interleaved: a quarter-size trace updates one pixel of each
    // 2x2 block of the history per frame, cycling through the block in InterleavePeriod frames, and the resolve keeps
    // the other three from the reprojected history. Clouds change slowly and the history already accumulates many
    // frames, so a quarter of the trace costs a quarter and converges close to a full-rate trace. The trace's
    // per-pixel jitter then advances once per period (TraceJitterFrame), so each pixel still sees a golden-ratio
    // sequence of step offsets.
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
        // Frames an interleaved trace takes to update every history pixel once.
        static constexpr std::uint32_t InterleavePeriod = 4;
        // Weight of a traced pixel in an interleaved trace. Each pixel is fresh one frame in four, so matching the
        // full-rate weight's time constant (1 - 0.9^4) would average a quarter of the samples, and the image would be
        // twice as noisy. 0.15 keeps the noise close to a full-rate trace's and follows a change over about 24 frames,
        // against a full-rate trace's 10; the resolve's clip box still rejects history the clouds have left.
        static constexpr float InterleavedWeight = 0.15f;

        // How a volume traces this frame.
        struct TracePlan
        {
            std::uint32_t width = 0, height = 0; // the trace target
            bool jitter = false;      // per-pixel step offsets: the history will reproject this trace
            bool interleaved = false; // one history pixel of each 2x2 block, at `phase`
            glm::uvec2 phase{0u};
        };

        // The pixel of each 2x2 block an interleaved trace updates on `frame`. Diagonal pairs come first, so two
        // consecutive frames already cover the block evenly.
        [[nodiscard]] static glm::uvec2 InterleavePhase(std::uint32_t frame) noexcept;
        // The frame index the trace's per-pixel jitter advances with (VolumetricCloud.slang cloudPixelJitter).
        [[nodiscard]] static std::uint32_t TraceJitterFrame(const TracePlan &plan, std::uint32_t frame) noexcept
        {
            return plan.interleaved ? frame / InterleavePeriod : frame;
        }

        bool Initialize(rhi::IRenderDevice &device, const Shaders &shaders);
        void Shutdown();
        [[nodiscard]] bool Available() const noexcept { return m_device && m_pipeline; }

        // continuous is false on the first frame or after a camera cut: every history restarts.
        void BeginFrame(const Camera &camera, bool continuous);
        // What a volume looks like apart from where its noise has drifted: its lighting, colour, density and shape
        // parameters. A jump in it (a light switched, a weather preset applied) restarts the volume's history, as a
        // camera cut does, so the change shows at once instead of through the history's slow blend; gradual
        // changes such as the time of day accumulate as before. Call before PlanTrace.
        static constexpr std::size_t AppearanceLanes = 6;
        using Appearance = std::array<glm::vec4, AppearanceLanes>;
        void ObserveAppearance(std::size_t volume, const Appearance &appearance);
        // On by default; off traces every pixel every frame (for comparisons).
        void SetInterleavedTrace(bool enabled) noexcept { m_interleave = enabled; }
        // How volume traces this frame for a width x height history (the volumetric pass's resolution).
        [[nodiscard]] TracePlan PlanTrace(std::size_t volume, std::uint32_t width, std::uint32_t height,
                                          std::uint32_t frame) const;
        // Accumulates the trace (rgb radiance, a transmittance), traced as `plan`, into volume's width x height
        // history and returns the result, valid until the same volume's next Resolve. Call outside rendering.
        [[nodiscard]] rhi::TextureHandle Resolve(rhi::ICommandContext &commands, std::size_t volume,
                                                 rhi::TextureHandle trace, const TracePlan &plan,
                                                 rhi::SamplerHandle sampler, std::uint32_t width, std::uint32_t height,
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
            Appearance appearance{};
            bool observed = false;
        };

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        std::vector<History> m_histories;
        Camera m_camera;
        bool m_continuous = false;
        bool m_interleave = true;

        // Whether volume's history will reproject a width x height trace this frame.
        [[nodiscard]] bool Reprojects(std::size_t volume, std::uint32_t width, std::uint32_t height) const;
    };
}
