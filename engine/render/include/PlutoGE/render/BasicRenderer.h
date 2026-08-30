#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    struct BasicVertex
    {
        std::array<float, 3> position{};
        std::array<float, 3> normal{};
        std::array<float, 2> uv{};
        // A valid fallback avoids undefined normalization for procedural or legacy meshes
        // that do not provide tangent data. Imported meshes overwrite this value.
        std::array<float, 4> tangent{1.0f, 0.0f, 0.0f, 1.0f};
    };

    struct BasicMeshData
    {
        std::span<const BasicVertex> vertices;
        std::span<const std::uint32_t> indices;
    };

    struct BasicRendererShaderPackage
    {
        rhi::GraphicsPipelineDescriptor::ShaderCode vertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode fragment;
        rhi::GraphicsPipelineDescriptor::ShaderCode shadowVertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode shadowFragment;
    };

    class BasicMesh
    {
    public:
        BasicMesh() = default;
        BasicMesh(BasicMesh &&) noexcept = default;
        BasicMesh &operator=(BasicMesh &&) noexcept = default;
        BasicMesh(const BasicMesh &) = delete;
        BasicMesh &operator=(const BasicMesh &) = delete;
        [[nodiscard]] bool IsValid() const noexcept { return m_vertexBuffer && m_indexBuffer && m_indexCount != 0; }

    private:
        friend class BasicRenderer;
        rhi::Buffer m_vertexBuffer;
        rhi::Buffer m_indexBuffer;
        std::uint32_t m_indexCount = 0;
    };

    struct BasicDraw
    {
        const BasicMesh *mesh = nullptr;
        glm::mat4 model{1.0f};
        glm::vec4 baseColor{1.0f};
        glm::vec2 uvScale{1.0f};
        rhi::TextureHandle baseColorTexture;
        rhi::TextureHandle normalTexture;
        rhi::TextureHandle metallicTexture;
        rhi::TextureHandle roughnessTexture;
        float metallic = 0.0f;
        float roughness = 1.0f;
        glm::vec3 emission{0.0f};
        float alphaCutoff = 0.5f;
        std::uint32_t alphaMode = 0;
        std::uint32_t metallicChannel = 0;
        std::uint32_t roughnessChannel = 0;
        bool flipNormalY = false;
        bool castsShadow = true;
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    struct BasicLighting
    {
        glm::vec3 cameraPosition{0.0f};
        float ambientIntensity = 0.3f;
        glm::vec3 directionalDirection{0.4f, -0.8f, 0.3f};
        float directionalIntensity = 1.0f;
        glm::vec3 directionalColor{1.0f};
        bool shadowsEnabled = false;
        glm::mat4 lightViewProjection{1.0f};
        bool shadowFlipY = false;
        float shadowDepthScale = 1.0f;
        float shadowDepthBias = 0.0f;
    };

    class BasicRenderer
    {
    public:
        BasicRenderer() = default;
        ~BasicRenderer() = default;
        BasicRenderer(const BasicRenderer &) = delete;
        BasicRenderer &operator=(const BasicRenderer &) = delete;

        bool Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders);
        void Shutdown();
        [[nodiscard]] BasicMesh CreateMesh(const BasicMeshData &data);
        bool Resize(std::uint32_t width, std::uint32_t height);
        void Render(const glm::mat4 &viewProjection, std::span<const BasicDraw> draws);
        void Render(const glm::mat4 &viewProjection, const BasicLighting &lighting, std::span<const BasicDraw> draws);

        [[nodiscard]] rhi::TextureHandle GetColorTexture() const noexcept { return m_colorTarget.Get(); }
        [[nodiscard]] std::uint32_t GetWidth() const noexcept { return m_width; }
        [[nodiscard]] std::uint32_t GetHeight() const noexcept { return m_height; }
        [[nodiscard]] bool IsInitialized() const noexcept { return m_device != nullptr; }

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::GraphicsPipeline m_shadowPipeline;
        rhi::Buffer m_cameraBuffer;
        rhi::Buffer m_shadowCameraBuffer;
        // Vulkan records the complete frame before execution, so every draw
        // needs stable object data until submission completes.
        std::vector<rhi::Buffer> m_objectBuffers;
        std::vector<rhi::Buffer> m_materialBuffers;
        rhi::Texture m_fallbackTexture;
        rhi::Texture m_fallbackNormalTexture;
        rhi::Texture m_fallbackDataTexture;
        rhi::Sampler m_fallbackSampler;
        rhi::Texture m_colorTarget;
        rhi::Texture m_depthTarget;
        rhi::Texture m_shadowColorTarget;
        rhi::Texture m_shadowDepthTarget;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
    };
}
