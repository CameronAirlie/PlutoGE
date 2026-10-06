#include "RhiGpuSkinning.h"
#include "PlutoGE/render/BasicRenderer.h"
#include <cstddef>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace PlutoGE::render
{
    namespace
    {
        // Byte-address buffers deliberately avoid backend-dependent float3
        // alignment. Keep these layouts in sync with GpuSkinning.slang.
        struct SourceVertex
        {
            std::array<float, 3> position, normal;
            std::array<float, 2> uv;
            std::array<float, 4> tangent;
            std::array<float, 2> uv2;
            std::array<int, 4> joints;
            std::array<float, 4> weights;
        };
        static_assert(sizeof(SourceVertex) == 88 && offsetof(SourceVertex, joints) == 56 && offsetof(SourceVertex, weights) == 72);
        static_assert(sizeof(BasicVertex) == 72 && offsetof(BasicVertex, normal) == 12 && offsetof(BasicVertex, uv) == 24 &&
                      offsetof(BasicVertex, tangent) == 32 && offsetof(BasicVertex, previousPosition) == 48 && offsetof(BasicVertex, uv2) == 64);
        static_assert(sizeof(glm::mat4) == 64);
        template<class T, std::size_t Extent> auto Bytes(std::span<T,Extent> values) { return std::as_bytes(values); }
    }

    bool RhiGpuSkinning::Initialize(rhi::IRenderDevice &device, const rhi::ComputePipelineDescriptor::ShaderCode &shader)
    {
        const bool available = device.GetApi() == rhi::GraphicsApi::Vulkan ? !shader.spirv.empty() : !shader.glsl.empty();
        if (!available) return false;
        rhi::ComputePipelineDescriptor descriptor;
        descriptor.computeShader = shader;
        descriptor.debugName = "GPU skeletal deformation";
        descriptor.resourceBindings = {
            {0,0,0,rhi::ResourceBindingType::UniformBuffer,rhi::ShaderStageMask::Compute},
            {1,0,1,rhi::ResourceBindingType::StorageBuffer,rhi::ShaderStageMask::Compute},
            {2,0,2,rhi::ResourceBindingType::StorageBuffer,rhi::ShaderStageMask::Compute},
            {3,0,3,rhi::ResourceBindingType::StorageBuffer,rhi::ShaderStageMask::Compute}};
        m_pipeline = rhi::GraphicsPipeline(device, device.CreateComputePipeline(descriptor));
        if (!m_pipeline) return false;
        m_device = &device;
        return true;
    }

    std::shared_ptr<RhiGpuSkinningSource> RhiGpuSkinning::CreateSource(std::span<const MeshVertexData> vertices)
    {
        if (!m_device || vertices.empty() || vertices.size() > std::numeric_limits<std::uint32_t>::max() / 88)
            throw std::invalid_argument("Invalid GPU skinning source");
        std::vector<SourceVertex> packed;
        packed.reserve(vertices.size());
        for (const auto &v : vertices) packed.push_back({v.position,v.normal,v.uv,v.tangent,v.uv2,v.joints,v.weights});
        auto result = std::make_shared<RhiGpuSkinningSource>();
        result->vertexCount = static_cast<std::uint32_t>(vertices.size());
        result->vertices = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {packed.size() * sizeof(SourceVertex),rhi::BufferUsage::Storage,"Skinning bind vertices",true}, Bytes(std::span(packed))));
        return result;
    }

    std::shared_ptr<RhiGpuSkinningState> RhiGpuSkinning::CreateState(std::shared_ptr<const RhiGpuSkinningSource> source)
    {
        if (!m_device || !source || !source->vertices) throw std::invalid_argument("Invalid GPU skinning state");
        auto result = std::make_shared<RhiGpuSkinningState>();
        result->source = std::move(source);
        result->parameters = rhi::Buffer(*m_device, m_device->CreateBuffer({16,rhi::BufferUsage::Uniform,"Skinning parameters"}));
        return result;
    }

    void RhiGpuSkinning::Queue(RhiGpuSkinningState &state, std::span<const glm::mat4> current, std::span<const glm::mat4> previous)
    {
        if (!m_device || current.empty() || current.size() != previous.size() ||
            current.size() > std::numeric_limits<std::uint32_t>::max() / 128)
            throw std::invalid_argument("Invalid GPU skinning palettes");
        if (state.jointCount != current.size())
            state.palette = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {current.size_bytes() * 2,rhi::BufferUsage::Storage,"Skinning bone palettes"}));
        state.matrices.assign(current.begin(), current.end());
        state.matrices.insert(state.matrices.end(), previous.begin(), previous.end());
        state.jointCount = static_cast<std::uint32_t>(current.size());
        state.pending = true;
    }

    bool RhiGpuSkinning::Record(RhiGpuSkinningState &state, rhi::BufferHandle output)
    {
        if (!state.pending) return false;
        auto &commands = m_device->GetImmediateContext();
        m_device->UpdateBuffer(state.palette.Get(), 0, Bytes(std::span(state.matrices)));
        const std::array<std::uint32_t,4> parameters{state.source->vertexCount,state.jointCount,0,0};
        m_device->UpdateBuffer(state.parameters.Get(), 0, Bytes(std::span(parameters)));
        commands.BindPipeline(m_pipeline.Get());
        commands.BindUniformBuffer(0,state.parameters.Get());
        commands.BindStorageBuffer(1,state.source->vertices.Get());
        commands.BindStorageBuffer(2,state.palette.Get());
        commands.BindStorageBuffer(3,output);
        // 2D dispatch avoids Vulkan's minimum 65535 workgroup limit on X.
        const auto groups = (state.source->vertexCount + 63u) / 64u;
        commands.Dispatch(std::min(groups,65535u),(groups + 65534u) / 65535u,1);
        state.pending = false;
        return true;
    }
}
