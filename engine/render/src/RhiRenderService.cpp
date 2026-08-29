#include "PlutoGE/render/RhiRenderService.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace PlutoGE::render
{
    namespace
    {
        std::string ReadTextShader(const char *fileName)
        {
            std::ifstream input(std::filesystem::path(PLUTO_RHI_SHADER_DIR) / fileName, std::ios::binary);
            std::ostringstream contents;
            contents << input.rdbuf();
            return contents.str();
        }

        std::vector<std::uint32_t> ReadSpirvShader(const char *fileName)
        {
            std::ifstream input(std::filesystem::path(PLUTO_RHI_SHADER_DIR) / fileName, std::ios::binary | std::ios::ate);
            if (!input) return {};
            const auto byteCount = input.tellg();
            if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0)
                return {};
            std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
            input.seekg(0);
            input.read(reinterpret_cast<char *>(words.data()), byteCount);
            return input ? words : std::vector<std::uint32_t>{};
        }
    }

    bool RhiRenderService::Initialize(rhi::IRenderDevice &device, rhi::ISwapchain &swapchain)
    {
        Shutdown();
        BasicRendererShaderPackage shaders;
        shaders.vertex.glsl = ReadTextShader("BasicLit.vertex.glsl");
        shaders.fragment.glsl = ReadTextShader("BasicLit.fragment.glsl");
        shaders.vertex.spirv = ReadSpirvShader("BasicLit.vertex.spv");
        shaders.fragment.spirv = ReadSpirvShader("BasicLit.fragment.spv");

        auto renderer = std::make_unique<BasicRenderer>();
        if (!renderer->Initialize(device, shaders) || !renderer->Resize(swapchain.GetWidth(), swapchain.GetHeight()))
            return false;
        m_graphicsApi = device.GetApi();
        m_swapchain = &swapchain;
        m_renderer = std::move(renderer);
        return true;
    }

    void RhiRenderService::Shutdown()
    {
        if (m_renderer)
            m_renderer->Shutdown();
        m_renderer.reset();
        m_swapchain = nullptr;
    }

    bool RhiRenderService::Resize(std::uint32_t width, std::uint32_t height)
    {
        return m_renderer && m_swapchain && m_swapchain->Resize(width, height) &&
               m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight());
    }

    bool RhiRenderService::RenderAndPresent(const glm::mat4 &viewProjection,
                                            const BasicLighting &lighting,
                                            std::span<const BasicDraw> draws)
    {
        if (!m_renderer || !m_swapchain)
            return false;
        if (m_renderer->GetWidth() != m_swapchain->GetWidth() || m_renderer->GetHeight() != m_swapchain->GetHeight())
            if (!m_renderer->Resize(m_swapchain->GetWidth(), m_swapchain->GetHeight()))
                return false;
        m_renderer->Render(viewProjection, lighting, draws);
        return m_swapchain->Present(m_renderer->GetColorTexture());
    }

    BasicMesh RhiRenderService::CreateMesh(const BasicMeshData &data)
    {
        if (!m_renderer)
            throw std::logic_error("RHI render service is not initialized");
        return m_renderer->CreateMesh(data);
    }
}
