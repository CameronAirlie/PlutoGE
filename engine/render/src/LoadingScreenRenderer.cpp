#include "PlutoGE/render/LoadingScreenRenderer.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include <algorithm>
#include <cmath>
#include <string_view>

namespace PlutoGE::render
{
    namespace
    {
        // Maximum 32 title letters + 7 label letters, each a 5x7 pixel glyph.
        constexpr std::size_t kMaxVertices = (39 * 35 + 16) * 6;
        constexpr unsigned glyphs[26][7] = {
                {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
                {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
                {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
                {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
                {17,27,21,21,17,17,17},{17,25,25,21,19,19,17},{14,17,17,17,17,17,14},
                {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
                {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
                {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
                {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
    }

    bool LoadingScreenRenderer::Initialize(rhi::IRenderDevice &device)
    {
        Shutdown();
        const ShaderArtifactLibrary shaders;
        rhi::GraphicsPipelineDescriptor descriptor;
        descriptor.vertexShader = shaders.Load("LoadingScreen", "vertex");
        descriptor.fragmentShader = shaders.Load("LoadingScreen", "fragment");
        descriptor.colorFormat = rhi::Format::R8G8B8A8Unorm;
        descriptor.depthFormat = rhi::Format::Undefined;
        descriptor.depthTest = false;
        descriptor.depthWrite = false;
        descriptor.cullMode = rhi::CullMode::None;
        descriptor.vertexLayout = {sizeof(Vertex), {
            {0, rhi::Format::R32G32Float, offsetof(Vertex, x)},
            {1, rhi::Format::R32G32B32Float, offsetof(Vertex, color)}}};
        descriptor.resourceBindings = {{0, 0, 0, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Vertex}};
        descriptor.debugName = "Loading screen";
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
        m_vertexBuffer = rhi::Buffer(device, device.CreateBuffer(
            {kMaxVertices * sizeof(Vertex), rhi::BufferUsage::Vertex, "Loading screen vertices"}));
        m_parameters = rhi::Buffer(device, device.CreateBuffer(
            {sizeof(float)*4, rhi::BufferUsage::Uniform, "Loading screen transform"}));
        if (!m_pipeline || !m_vertexBuffer || !m_parameters) { Shutdown(); return false; }
        m_vertices.reserve(kMaxVertices);
        m_device = &device;
        return true;
    }

    void LoadingScreenRenderer::Shutdown()
    {
        m_color.Reset();
        m_vertexBuffer.Reset();
        m_parameters.Reset();
        m_pipeline.Reset();
        m_vertices.clear();
        m_device = nullptr;
        m_width = m_height = 0;
    }

    bool LoadingScreenRenderer::Render(std::uint32_t width, std::uint32_t height, float elapsedSeconds,
                                       unsigned stage, const LoadingScreenStyle &style, bool drawDefault)
    {
        if (!m_device || width == 0 || height == 0) return false;
        if (width != m_width || height != m_height)
        {
            auto color = rhi::Texture(*m_device, m_device->CreateTexture({
                .width = width, .height = height, .format = rhi::Format::R8G8B8A8Unorm,
                .usage = rhi::TextureUsage::ColorAttachment, .debugName = "Loading screen output",
                .sampled = true, .mipLevels = 1}));
            if (!color) return false;
            m_color = std::move(color);
            m_width = width;
            m_height = height;
        }
        m_vertices.clear();
        const float aspect = static_cast<float>(width) / height;
        const float ySign = m_device->GetApi() == rhi::GraphicsApi::OpenGL ? 1.0f : -1.0f;
        const auto rectangle = [&](float x, float y, float w, float h, glm::vec3 color)
        {
            const Vertex corners[] = {{x, y, color}, {x+w, y, color},
                                      {x+w, y+h, color}, {x, y+h, color}};
            for (unsigned index : {0u, 1u, 2u, 0u, 2u, 3u}) m_vertices.push_back(corners[index]);
        };
        const auto text = [&](std::string_view label, float y, float size, glm::vec3 color)
        {
            const float left = -float(label.size()*6-1)*size*.5f;
            for (std::size_t letter = 0; letter < label.size(); ++letter)
            {
                char c = label[letter];
                if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
                if (c < 'A' || c > 'Z') continue;
                for (int row = 0; row < 7; ++row)
                    for (int col = 0; col < 5; ++col)
                        if (glyphs[c-'A'][row] & (16 >> col))
                            rectangle(left + float(letter*6+col)*size, y+float(6-row)*size, size, size, color);
            }
        };
        if (drawDefault)
        {
            const auto title = std::string_view(style.title).substr(0, 32);
            if (!title.empty())
            {
                text(title, .32f, std::min(.032f, 1.8f/float(title.size()*6-1)), style.accent);
                rectangle(-.65f, .25f, 1.3f, .004f, style.accent*.35f);
            }
            text("LOADING", .08f, .02f, {.70f,.72f,.75f});
            rectangle(-.5f, -.12f, 1, .025f, {.025f,.03f,.04f});
            const float phase = .5f + .5f*std::sin(elapsedSeconds*2.5f);
            rectangle(-.5f+.8f*phase, -.12f, .2f, .025f, style.accent);
            for (unsigned i = 0; i < 3; ++i)
                rectangle(-.06f+.05f*i, -.22f, .02f, .02f, i < stage ? style.accent : glm::vec3(.05f));

        }

        auto &commands = m_device->GetImmediateContext();
        commands.BeginFrame("Loading screen");
        try
        {
            // Record uploads before rendering. Vulkan snapshots vertex data into the
            // command stream so animation cannot overwrite an in-flight frame.
            if (!m_vertices.empty()) m_device->UpdateBuffer(m_vertexBuffer.Get(), 0, std::as_bytes(std::span(m_vertices)));
            const std::array<float, 4> scale{1.0f/aspect, ySign, 0, 0};
            m_device->UpdateBuffer(m_parameters.Get(), 0, std::as_bytes(std::span(scale)));
            rhi::RenderingInfo target;
            target.colorAttachments = {m_color.Get()};
            target.width = width;
            target.height = height;
            target.clearDepth = false;
            target.clearColorValue[0] = .035f;
            target.clearColorValue[1] = .043f;
            target.clearColorValue[2] = .055f;
            commands.BeginRendering(target);
            commands.SetViewport({0, 0, float(width), float(height), 0, 1});
            commands.SetScissor({0, 0, width, height});
            commands.BindPipeline(m_pipeline.Get());
            commands.BindVertexBuffer(m_vertexBuffer.Get());
            commands.BindUniformBuffer(0, m_parameters.Get());
            if (!m_vertices.empty()) commands.Draw(static_cast<std::uint32_t>(m_vertices.size()));
            commands.EndRendering();
            commands.Submit();
        }
        catch (...)
        {
            commands.RecoverInterruptedFrame();
            throw;
        }
        return true;
    }
}
