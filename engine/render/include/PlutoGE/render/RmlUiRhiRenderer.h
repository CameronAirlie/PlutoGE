#pragma once

#include "PlutoGE/render/rhi/Resource.h"
#include "PlutoGE/render/rhi/RenderDevice.h"

#include <RmlUi/Core/Matrix4.h>
#include <RmlUi/Core/RenderInterface.h>

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>

namespace PlutoGE::render
{
    // Basic RmlUi renderer used by both RHI backends. It intentionally
    // implements RmlUi's core geometry, texture, transform, and scissor
    // and clip-mask contract; advanced layer filters remain on the legacy GL3 renderer.
    class RmlUiRhiRenderer final : public Rml::RenderInterface
    {
    public:
        // Shared binding keeps UI handles valid when a producer replaces or releases its target.
        struct ExternalTexture
        {
            rhi::Texture resource;
            int width = 0, height = 0;
            bool flipY = false;
        };
        void RegisterExternalTexture(const std::string &source, std::shared_ptr<ExternalTexture> texture);
        void UnregisterExternalTexture(const std::string &source);
        RmlUiRhiRenderer(rhi::IRenderDevice &device,
                         const rhi::GraphicsPipelineDescriptor::ShaderCode &vertexShader,
                         const rhi::GraphicsPipelineDescriptor::ShaderCode &fragmentShader);
        ~RmlUiRhiRenderer() override;

        explicit operator bool() const noexcept { return static_cast<bool>(m_pipeline); }
        void SetViewport(int width, int height);
        // 2x per axis (four coverage samples), filtered back to native size.
        // Layout, input coordinates and scene resolution remain unchanged.
        void SetAntialiasingEnabled(bool enabled) { m_antialiasingEnabled = enabled; }
        // The flags are false when UI is appended to an already active scene frame.
        void BeginFrame(rhi::TextureHandle target, bool beginSubmission = true);
        void EndFrame(bool submit = true);
        // CPU-side state only; the owning service recovers the command context.
        void CancelFrame() noexcept { m_frameActive = false; m_outputTarget = {}; m_maskMode = 0; }

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
        void EnableClipMask(bool enable) override;
        void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry,
                              Rml::Vector2f translation) override;

    private:
        struct Geometry;
        struct Texture;
        struct Parameters;
        rhi::Buffer &AcquireParameterBuffer(const Parameters &parameters);
        void ApplyScissor();

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::GraphicsPipeline m_clipPipeline;
        rhi::Texture m_clipTargets[2];
        int m_clipWidth = 0, m_clipHeight = 0, m_clipIndex = 0;
        bool m_clipEnabled = false, m_clipValid = false;
        float m_maskMode = 0;
        rhi::Sampler m_sampler;
        rhi::Texture m_uiTarget;
        rhi::Buffer m_compositeVertices;
        rhi::TextureHandle m_outputTarget;
        int m_targetWidth = 0, m_targetHeight = 0;
        int m_renderScale = 1;
        bool m_antialiasingEnabled = true;
        std::unique_ptr<Texture> m_whiteTexture;
        std::unordered_map<std::string, std::shared_ptr<ExternalTexture>> m_externalTextures;
        std::vector<rhi::Buffer> m_parameterBuffers;
        std::vector<Parameters> m_parameterValues;
        std::size_t m_parameterCursor = 0;
        Rml::Matrix4f m_transform;
        Rml::Rectanglei m_scissor;
        Rml::Rectanglei m_clipRectangle = Rml::Rectanglei::MakeInvalid();
        int m_width = 1;
        int m_height = 1;
        bool m_scissorEnabled = false;
        bool m_frameActive = false;
    };
}
