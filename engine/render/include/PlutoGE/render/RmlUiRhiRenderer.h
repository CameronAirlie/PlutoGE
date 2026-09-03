#pragma once

#include "PlutoGE/render/rhi/Resource.h"
#include "PlutoGE/render/rhi/RenderDevice.h"

#include <RmlUi/Core/Matrix4.h>
#include <RmlUi/Core/RenderInterface.h>

#include <memory>
#include <vector>

namespace PlutoGE::render
{
    // Basic RmlUi renderer used by both RHI backends. It intentionally
    // implements RmlUi's core geometry, texture, transform, and scissor
    // contract; advanced layer filters remain on the legacy GL3 renderer.
    class RmlUiRhiRenderer final : public Rml::RenderInterface
    {
    public:
        RmlUiRhiRenderer(rhi::IRenderDevice &device,
                         const rhi::GraphicsPipelineDescriptor::ShaderCode &vertexShader,
                         const rhi::GraphicsPipelineDescriptor::ShaderCode &fragmentShader);
        ~RmlUiRhiRenderer() override;

        explicit operator bool() const noexcept { return static_cast<bool>(m_pipeline); }
        void SetViewport(int width, int height);
        // The flags are false when UI is appended to an already active scene frame.
        void BeginFrame(rhi::TextureHandle target, bool beginSubmission = true);
        void EndFrame(bool submit = true);

        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                    Rml::Span<const int> indices) override;
        void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                            Rml::TextureHandle texture) override;
        void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
        Rml::TextureHandle LoadTexture(Rml::Vector2i &textureDimensions, const Rml::String &source) override;
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                           Rml::Vector2i sourceDimensions) override;
        void ReleaseTexture(Rml::TextureHandle texture) override;
        void EnableScissorRegion(bool enable) override;
        void SetScissorRegion(Rml::Rectanglei region) override;
        void SetTransform(const Rml::Matrix4f *transform) override;

    private:
        struct Geometry;
        struct Texture;
        struct Parameters;
        rhi::Buffer &AcquireParameterBuffer();
        void ApplyScissor();

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::Sampler m_sampler;
        std::unique_ptr<Texture> m_whiteTexture;
        std::vector<rhi::Buffer> m_parameterBuffers;
        std::size_t m_parameterCursor = 0;
        Rml::Matrix4f m_transform;
        Rml::Rectanglei m_scissor;
        int m_width = 1;
        int m_height = 1;
        bool m_scissorEnabled = false;
        bool m_frameActive = false;
    };
}
