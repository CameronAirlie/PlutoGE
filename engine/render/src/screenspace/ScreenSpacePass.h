#pragma once

#include "../PostProcessParameterBlock.h"
#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace PlutoGE::render
{
    // Infrastructure shared by the screen-space passes (ScreenSpaceGuide, ScreenSpaceAmbientOcclusion,
    // ScreenSpaceReflections). They depend only on the RHI and these types, never on BasicRenderer, so each can be
    // replaced or extended on its own.

    using ScreenSpaceShaderCode = rhi::GraphicsPipelineDescriptor::ShaderCode;
    struct ScreenSpaceFullscreenShader
    {
        ScreenSpaceShaderCode vertex, fragment;
    };

    // The camera, G-buffer and frame state a screen-space pass reads, captured once per frame by the renderer.
    // `environmentSpecular` is the environment reflection the scene colour currently holds (ScreenSpaceLighting).
    struct ScreenSpaceView
    {
        rhi::TextureHandle depth, normal, material, albedo, motion, environmentSpecular;
        rhi::SamplerHandle linearSampler, pointSampler;
        std::uint32_t width = 0, height = 0;
        glm::mat4 view{1.0f}, projection{1.0f}, inverseViewProjection{1.0f};
        // The previous frame's view and unjittered view-projection (the current ones on a first frame).
        glm::mat4 previousView{1.0f}, previousViewProjection{1.0f};
        glm::vec4 cameraPosition{0.0f};
        std::uint64_t frameIndex = 0;
        float time = 0.0f;
        bool flipY = false, zeroToOneDepth = false;
    };

    [[nodiscard]] bool HasShader(const ScreenSpaceShaderCode &code) noexcept;
    [[nodiscard]] bool HasShader(const ScreenSpaceFullscreenShader &shader) noexcept;

    // The shared parameter block (PostProcessParameters.slang) for `view`, with effect lanes and quality.
    [[nodiscard]] BasicPostProcessParameters MakeScreenSpaceParameters(
        const ScreenSpaceView &view, std::uint32_t quality,
        const std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> &lanes);

    // Scene colour and the ambient light it contains: the geometry pass's indirect target (diffuse) and environment
    // specular target (reflections, alpha their sky share), or what a global illumination pass replaced them with.
    // Passes that relight ambient light (DDGI, SSGI) read and return them, passing on terms they leave unchanged, so
    // the next one, SSAO and SSR always see the terms the colour actually holds. `environmentSpecular` is empty when
    // the geometry pass did not write it.
    struct ScreenSpaceLighting
    {
        rhi::TextureHandle color, indirect, environmentSpecular;
    };

    // Uniform buffers hold one value per frame, so every draw or dispatch in a frame takes its own buffer. Buffers
    // are created on demand and reused from frame to frame. An instance pushes values of a single type.
    class ScreenSpaceUniforms
    {
    public:
        void Initialize(rhi::IRenderDevice &device, std::string name);
        void BeginFrame() noexcept { m_next = 0; }
        template <class Value>
        [[nodiscard]] rhi::BufferHandle Push(const Value &value)
        {
            return PushBytes(std::as_bytes(std::span(&value, 1)));
        }
        [[nodiscard]] rhi::BufferHandle PushBytes(std::span<const std::byte> bytes);
        void Reset() noexcept;

    private:
        rhi::IRenderDevice *m_device = nullptr;
        std::string m_name;
        std::vector<rhi::Buffer> m_buffers;
        std::size_t m_next = 0;
    };

    // A texture recreated whenever its extent changes.
    class ScreenSpaceTarget
    {
    public:
        // True when the texture was (re)created, so content and history derived from it are gone.
        bool Ensure(rhi::IRenderDevice &device, const rhi::TextureDescriptor &descriptor);
        [[nodiscard]] rhi::TextureHandle Get() const noexcept { return m_texture.Get(); }
        [[nodiscard]] std::uint32_t Width() const noexcept { return m_width; }
        [[nodiscard]] std::uint32_t Height() const noexcept { return m_height; }
        void Reset() noexcept;

    private:
        rhi::Texture m_texture;
        std::uint32_t m_width = 0, m_height = 0;
    };

    // Fullscreen-triangle pipeline: uniform block at slot 0 and the given sampled-texture slots, no depth.
    [[nodiscard]] rhi::GraphicsPipeline CreateScreenSpacePipeline(
        rhi::IRenderDevice &device, const ScreenSpaceFullscreenShader &shader,
        std::initializer_list<std::uint32_t> textureSlots, rhi::Format colorFormat, const char *debugName);
    // The same, writing one target per format.
    [[nodiscard]] rhi::GraphicsPipeline CreateScreenSpacePipeline(
        rhi::IRenderDevice &device, const ScreenSpaceFullscreenShader &shader,
        std::initializer_list<std::uint32_t> textureSlots, std::initializer_list<rhi::Format> colorFormats,
        const char *debugName);

    struct ScreenSpaceComputeBindings
    {
        std::vector<std::uint32_t> textures, storageImages, storageBuffers;
    };
    // Compute pipeline with a uniform block at slot 0.
    [[nodiscard]] rhi::GraphicsPipeline CreateScreenSpaceComputePipeline(
        rhi::IRenderDevice &device, const ScreenSpaceShaderCode &shader, const ScreenSpaceComputeBindings &bindings,
        const char *debugName);

    using ScreenSpaceTextureBinding = std::pair<std::uint32_t, rhi::TextureHandle>;
    // Draws a fullscreen triangle into `target` (no clear), binding `uniforms` at slot 0.
    void DrawScreenSpacePass(rhi::ICommandContext &commands, rhi::TextureHandle target, std::uint32_t width,
                             std::uint32_t height, rhi::PipelineHandle pipeline, rhi::BufferHandle uniforms,
                             std::initializer_list<ScreenSpaceTextureBinding> textures, rhi::SamplerHandle sampler);
    // The same into several targets of one extent.
    void DrawScreenSpacePass(rhi::ICommandContext &commands, std::initializer_list<rhi::TextureHandle> targets,
                             std::uint32_t width, std::uint32_t height, rhi::PipelineHandle pipeline,
                             rhi::BufferHandle uniforms, std::initializer_list<ScreenSpaceTextureBinding> textures,
                             rhi::SamplerHandle sampler);
}
