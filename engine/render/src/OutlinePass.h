#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Outline.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace PlutoGE::render
{
    // Screen-space outlines (SelectionOutline.slang, docs/OUTLINES.md), one layer per style. Each layer's draws are
    // rasterized depth-only into that layer's mask, without temporal jitter so the line does not shimmer; after the
    // display output is written, every layer is composited onto it in order, so later layers draw on top.
    //
    // The renderer records the masks with its own draw path (every vertex variant: skinned, instanced, graph
    // deformation), so this pass only owns what is specific to outlines: the composite pipeline, the per-layer masks
    // and parameters, and the unjittered camera constants the masks are drawn with.
    class OutlinePass
    {
    public:
        using ShaderCode = rhi::GraphicsPipelineDescriptor::ShaderCode;
        static constexpr auto MaskFormat = rhi::Format::D32Float;

        // `cameraParametersSize` is the size of the frame constants the geometry pipelines read (uniform slot 0).
        bool Initialize(rhi::IRenderDevice &device, const ShaderCode &vertex, const ShaderCode &fragment,
                        std::size_t cameraParametersSize);
        [[nodiscard]] bool Available() const noexcept { return m_device != nullptr; }

        // Starts a frame: takes the first MaxOutlineLayers layers that have draws, allocates their width x height
        // masks (the render resolution) and stores `camera`, the frame constants without jitter. Returns how many
        // layers draw; zero costs nothing further.
        std::size_t Prepare(std::span<const BasicOutlineLayer> layers, std::uint32_t width, std::uint32_t height,
                            std::span<const std::byte> camera);
        [[nodiscard]] std::size_t LayerCount() const noexcept { return m_layerCount; }
        [[nodiscard]] std::span<const BasicDraw> Draws(std::size_t layer) const noexcept { return m_layers[layer].draws; }
        [[nodiscard]] rhi::TextureHandle Mask(std::size_t layer) const noexcept { return m_slots[layer].mask.Get(); }
        // The unjittered frame constants to draw masks with.
        [[nodiscard]] rhi::BufferHandle Camera() const noexcept { return m_camera.Get(); }

        // Composites every prepared layer onto `target` (outputWidth x outputHeight, R8G8B8A8Unorm), outside
        // rendering. `sceneDepth` is the frame's depth at the mask resolution: it decides which parts are hidden.
        void Composite(rhi::ICommandContext &commands, rhi::TextureHandle target, std::uint32_t outputWidth,
                       std::uint32_t outputHeight, rhi::TextureHandle sceneDepth, rhi::SamplerHandle sampler);
        // Forgets the borrowed layers; the next frame prepares its own.
        void EndFrame() noexcept;
        void Shutdown();

    private:
        struct Slot
        {
            rhi::Texture mask;
            rhi::Buffer parameters;
        };

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::Buffer m_camera;
        std::array<Slot, MaxOutlineLayers> m_slots;
        std::array<BasicOutlineLayer, MaxOutlineLayers> m_layers;
        std::size_t m_layerCount = 0;
        std::uint32_t m_width = 0, m_height = 0;
    };
}
