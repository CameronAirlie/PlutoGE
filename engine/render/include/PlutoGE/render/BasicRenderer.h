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
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
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

        [[nodiscard]] rhi::TextureHandle GetColorTexture() const noexcept { return m_colorTarget.Get(); }
        [[nodiscard]] std::uint32_t GetWidth() const noexcept { return m_width; }
        [[nodiscard]] std::uint32_t GetHeight() const noexcept { return m_height; }
        [[nodiscard]] bool IsInitialized() const noexcept { return m_device != nullptr; }

    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::Buffer m_cameraBuffer;
        rhi::Buffer m_objectBuffer;
        rhi::Texture m_fallbackTexture;
        rhi::Sampler m_fallbackSampler;
        rhi::Texture m_colorTarget;
        rhi::Texture m_depthTarget;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
    };
}
