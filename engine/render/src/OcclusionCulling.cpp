#include "PlutoGE/render/OcclusionCulling.h"
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace PlutoGE::render
{
    namespace
    {
        template<class T> auto Bytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }
        struct alignas(16) Parameters
        {
            glm::mat4 viewProjection{1};
            glm::uvec4 source{}, destination{}, config{};
            std::array<glm::uvec4, 32> levels{};
        };
        struct alignas(16) Candidate { glm::vec4 bounds, extents; glm::uvec4 draw; };
        static_assert(sizeof(Parameters) == 624 && sizeof(Candidate) == 48);
        bool Single(const BasicDraw &draw) { return !draw.instanceModels || draw.instanceModels->size() == 1; }
    }

    void OcclusionCulling::SetRigidBounds(BasicDraw &draw, glm::vec3 minimum, glm::vec3 maximum)
    {
        draw.occlusionBoundsExtents = glm::vec3(-1);
        if (!Single(draw)) return;
        const auto &model = draw.instanceModels ? draw.instanceModels->front() : draw.model;
        const glm::vec3 center = (minimum + maximum) * 0.5f;
        const glm::vec3 extents = (maximum - minimum) * 0.5f;
        if (glm::any(glm::lessThan(extents, glm::vec3(0)))) return;
        draw.occlusionBoundsCenter = glm::vec3(model * glm::vec4(center, 1));
        draw.occlusionBoundsExtents = glm::abs(glm::vec3(model[0])) * extents.x +
            glm::abs(glm::vec3(model[1])) * extents.y + glm::abs(glm::vec3(model[2])) * extents.z;
    }

    bool OcclusionCulling::SafeOccluder(const BasicDraw &draw)
    {
        return draw.mesh && draw.mesh->IsValid() && Single(draw) && !draw.shaderGraphProgram &&
            !draw.outlinePass && draw.surfaceType == 0 && draw.alphaMode == 0;
    }

    void OcclusionCulling::Initialize(rhi::IRenderDevice &device, const OcclusionShaders &shaders)
    {
        using namespace rhi;
        const auto present = [](const auto &code) { return !code.spirv.empty() || !code.glsl.empty(); };
        if (!device.GetImmediateContext().SupportsGpuOcclusionCulling() || !present(shaders.vertex) ||
            !present(shaders.fragment) || !present(shaders.reduce) || !present(shaders.test)) return;
        GraphicsPipelineDescriptor depth;
        depth.vertexShader = shaders.vertex; depth.fragmentShader = shaders.fragment;
        depth.colorFormat = Format::R32Float;
        // Match the main opaque pipeline's two-sided raster coverage.
        depth.cullMode = CullMode::None;
        depth.vertexLayout = {sizeof(BasicVertex), {{0, Format::R32G32B32Float, offsetof(BasicVertex, position)}}};
        depth.resourceBindings = {{0, 0, 0, ResourceBindingType::UniformBuffer, ShaderStageMask::Vertex},
                                  {1, 0, 1, ResourceBindingType::UniformBuffer, ShaderStageMask::Vertex}};
        depth.debugName = "Occlusion opaque depth";
        m_depthPipeline = GraphicsPipeline(device, device.CreateGraphicsPipeline(depth));
        std::vector<GraphicsPipelineDescriptor::ResourceBinding> bindings{
            {0, 0, 0, ResourceBindingType::UniformBuffer, ShaderStageMask::Compute},
            {1, 0, 1, ResourceBindingType::SampledTexture, ShaderStageMask::Compute}};
        for (std::uint32_t slot = 2; slot <= 5; ++slot)
            bindings.push_back({slot, 0, slot, ResourceBindingType::StorageBuffer, ShaderStageMask::Compute});
        m_reducePipeline = GraphicsPipeline(device, device.CreateComputePipeline({shaders.reduce, bindings, "Occlusion hierarchy"}));
        m_testPipeline = GraphicsPipeline(device, device.CreateComputePipeline({shaders.test, bindings, "Occlusion bounds test"}));
        m_sampler = Sampler(device, device.CreateSampler({false, false, "Occlusion point depth"}));
        m_counters = Buffer(device, device.CreateBuffer({64, BufferUsage::Storage, "Occlusion counters"}));
    }

    bool OcclusionCulling::Record(rhi::IRenderDevice &device, std::span<const BasicDraw> draws,
                                  const glm::mat4 &viewProjection, std::uint32_t width, std::uint32_t height,
                                  OcclusionMode mode, const SubmitDepth &submit, rhi::TextureHandle compatibleDepth)
    {
        using namespace rhi;
        if (mode == OcclusionMode::Off || !m_testPipeline || draws.empty() || width == 0 || height == 0) return false;
        auto &commands = device.GetImmediateContext();
        Parameters parameters;
        parameters.viewProjection = viewProjection;
        parameters.config = {draws.size(), (device.UsesZeroToOneClipDepth() ? 1u : 0u) | (device.GetApi() == GraphicsApi::Vulkan ? 2u : 0u), mode == OcclusionMode::Cull ? 1u : 0u, 0};
        std::uint32_t count = 0, w = width, h = height;
        do
        {
            w = (w + 1) / 2; h = (h + 1) / 2;
            parameters.levels[parameters.config.w++] = {w, h, count, 0};
            count += w * h;
        } while (w > 1 || h > 1);
        if (width != m_width || height != m_height)
        {
            m_depth = Texture(device, device.CreateTexture({width, height, Format::D32Float, TextureUsage::DepthStencilAttachment, "Occlusion depth", true}));
            m_color = Texture(device, device.CreateTexture({width, height, Format::R32Float, TextureUsage::ColorAttachment, "Occlusion depth color"}));
            m_hierarchy = Buffer(device, device.CreateBuffer({count * sizeof(float), BufferUsage::Storage, "Occlusion depth hierarchy"}));
            m_width = width; m_height = height;
        }
        if (draws.size() > m_capacity)
        {
            m_capacity = std::max(draws.size(), m_capacity * 2);
            m_candidates = Buffer(device, device.CreateBuffer({m_capacity * sizeof(Candidate), BufferUsage::Storage, "Occlusion bounds"}));
            m_indirect = Buffer(device, device.CreateBuffer({m_capacity * 20, BufferUsage::Storage, "Occlusion indirect draws"}));
        }
        std::vector<Candidate> candidates;
        candidates.reserve(draws.size());
        for (const auto &draw : draws)
        {
            const auto available = draw.mesh && draw.firstIndex < draw.mesh->GetIndexCount() ? draw.mesh->GetIndexCount() - draw.firstIndex : 0;
            const auto indices = std::min(draw.indexCount ? draw.indexCount : available, available);
            // Fragment-only graphs can be occludees even though their coverage
            // is unsuitable for the simplified occluder depth shader.
            const bool deformedGraph = draw.shaderGraphProgram && draw.shaderGraphProgram->data.header.z != 0;
            const bool eligible = draw.mesh && draw.mesh->IsValid() && indices != 0 && Single(draw) && !deformedGraph && !draw.outlinePass && draw.surfaceType == 0 && draw.alphaMode != 2;
            const bool tight = eligible && glm::all(glm::greaterThanEqual(draw.occlusionBoundsExtents, glm::vec3(0)));
            candidates.push_back({glm::vec4(tight ? draw.occlusionBoundsCenter : draw.shadowBoundsCenter,
                                            tight ? 0.0f : draw.shadowBoundsRadius),
                                  glm::vec4(tight ? draw.occlusionBoundsExtents : glm::vec3(draw.shadowBoundsRadius), 0),
                                  {indices, draw.firstIndex, eligible ? 1u : 0u, tight ? 1u : 0u}});
        }
        device.UpdateBuffer(m_candidates.Get(), 0, std::as_bytes(std::span(candidates)));
        const std::array<std::uint32_t, 16> zeros{};
        device.UpdateBuffer(m_counters.Get(), 0, Bytes(zeros));
        std::size_t uniform = 0;
        const auto bindParameters = [&]()
        {
            if (uniform == m_uniforms.size()) m_uniforms.emplace_back(device, device.CreateBuffer({sizeof(Parameters), BufferUsage::Uniform, "Occlusion parameters"}));
            auto handle = m_uniforms[uniform++].Get();
            device.UpdateBuffer(handle, 0, Bytes(parameters));
            commands.BindUniformBuffer(0, handle);
        };
        if (!compatibleDepth)
        {
            commands.BeginGpuScope("RHI Occlusion Depth");
            RenderingInfo info;
            info.colorAttachments = {m_color.Get()}; info.depthAttachment = m_depth.Get(); info.width = width; info.height = height;
            commands.BeginRendering(info);
            commands.BindPipeline(m_depthPipeline.Get());
            bindParameters();
            std::size_t modelIndex = 0;
            for (const auto &draw : draws)
            {
                if (!SafeOccluder(draw)) continue;
                if (modelIndex == m_models.size()) m_models.emplace_back(device, device.CreateBuffer({sizeof(glm::mat4), BufferUsage::Uniform, "Occlusion model"}));
                auto model = m_models[modelIndex++].Get();
                device.UpdateBuffer(model, 0, Bytes(draw.instanceModels ? draw.instanceModels->front() : draw.model));
                commands.BindUniformBuffer(1, model);
                submit(draw);
            }
            commands.EndRendering(); commands.EndGpuScope();
            compatibleDepth = m_depth.Get();
        }
        commands.BeginGpuScope("RHI Occlusion Hierarchy");
        commands.BindPipeline(m_reducePipeline.Get());
        commands.BindTexture(1, compatibleDepth, m_sampler.Get());
        commands.BindStorageBuffer(2, m_hierarchy.Get());
        commands.BindStorageBuffer(3, m_candidates.Get());
        commands.BindStorageBuffer(4, m_indirect.Get());
        commands.BindStorageBuffer(5, m_counters.Get());
        parameters.source = {width, height, 0, 1};
        for (std::uint32_t level = 0; level < parameters.config.w; ++level)
        {
            parameters.destination = parameters.levels[level];
            bindParameters();
            commands.Dispatch((parameters.destination.x + 7) / 8, (parameters.destination.y + 7) / 8, 1);
            commands.ShaderMemoryBarrier();
            parameters.source = parameters.destination;
        }
        commands.EndGpuScope();
        commands.BeginGpuScope("RHI Occlusion Test");
        commands.BindPipeline(m_testPipeline.Get());
        parameters.source = {width, height, 0, 0};
        bindParameters();
        commands.Dispatch((static_cast<std::uint32_t>(draws.size()) + 63) / 64, 1, 1);
        commands.ShaderMemoryBarrier();
        commands.EndGpuScope();
        const auto frame = ++m_frame;
        const std::weak_ptr<OcclusionStats> stats = m_stats;
        commands.QueueBufferReadback(m_counters.Get(), 64, [stats, frame](std::span<const std::byte> bytes)
        {
            if (const auto target = stats.lock(); target && bytes.size() == 64 && frame > target->frame)
            {
                std::array<std::uint32_t, 16> values{};
                std::memcpy(values.data(), bytes.data(), 64);
                *target = {values[0], values[1], values[2], frame, true,
                           values[3], values[4], values[5], values[6], values[7], values[8], values[9]};
            }
        });
        return true;
    }
}
