#include "RhiGpuSkinning.h"
#include "PlutoGE/render/BasicRenderer.h"
#include <cstddef>
#include <algorithm>
#include <cmath>
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
            std::array<float, 3> position;
            std::array<std::uint32_t, 4> frame; // snorm16: normal xyz, tangent xyzw, pad.
            std::array<float, 2> uv;
            std::array<float, 2> uv2;
            std::array<std::uint32_t, 2> joints; // u16x4; 0xffff is "no joint".
            std::array<float, 4> weights;
        };
        static_assert(sizeof(SourceVertex) == 68 && offsetof(SourceVertex, frame) == 12 && offsetof(SourceVertex, uv) == 28 &&
                      offsetof(SourceVertex, uv2) == 36 && offsetof(SourceVertex, joints) == 44 && offsetof(SourceVertex, weights) == 52);
        static_assert(sizeof(BasicVertex) == 72 && offsetof(BasicVertex, normal) == 12 && offsetof(BasicVertex, uv) == 24 &&
                      offsetof(BasicVertex, tangent) == 32 && offsetof(BasicVertex, previousPosition) == 48 && offsetof(BasicVertex, uv2) == 64);
        template<class T, std::size_t Extent> auto Bytes(std::span<T,Extent> values) { return std::as_bytes(values); }

        constexpr std::uint16_t NoJoint = 0xffff;
        constexpr std::uint32_t PaletteRowsPerJoint = 3;

        // Deformation output is renormalized and invariant to source vector
        // length, so normalizing first keeps full 16-bit precision. Degenerate
        // vectors stay zero and take the kernel's fallback frame.
        std::array<float, 3> NormalizedOrZero(const float *v)
        {
            const float squared = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
            if (!(squared > 1e-12f) || !std::isfinite(squared)) return {0, 0, 0};
            const float inverse = 1.0f / std::sqrt(squared);
            return {v[0] * inverse, v[1] * inverse, v[2] * inverse};
        }
        std::int16_t EncodeSnorm16(float value)
        {
            if (!std::isfinite(value)) value = 0;
            return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
        }
        float DecodeSnorm16(std::int16_t value) { return std::max(static_cast<float>(value) / 32767.0f, -1.0f); }
        std::uint16_t EncodeJoint(int joint)
        {
            return joint < 0 || joint >= static_cast<int>(NoJoint) ? NoJoint : static_cast<std::uint16_t>(joint);
        }
        std::uint32_t PackPair(std::uint16_t low, std::uint16_t high) { return std::uint32_t(low) | (std::uint32_t(high) << 16); }

        // Normal xyz then tangent xyzw, in the kernel's unpacking order.
        std::array<std::int16_t, 7> EncodeFrame(const MeshVertexData &v)
        {
            const auto normal = NormalizedOrZero(v.normal.data());
            const auto tangent = NormalizedOrZero(v.tangent.data());
            return {EncodeSnorm16(normal[0]), EncodeSnorm16(normal[1]), EncodeSnorm16(normal[2]),
                    EncodeSnorm16(tangent[0]), EncodeSnorm16(tangent[1]), EncodeSnorm16(tangent[2]), EncodeSnorm16(v.tangent[3])};
        }
        SourceVertex PackSource(const MeshVertexData &v)
        {
            const auto frame = EncodeFrame(v);
            const auto bits = [&](std::size_t i) { return static_cast<std::uint16_t>(frame[i]); };
            return {v.position,
                    {PackPair(bits(0), bits(1)), PackPair(bits(2), bits(3)), PackPair(bits(4), bits(5)), PackPair(bits(6), 0)},
                    v.uv, v.uv2,
                    {PackPair(EncodeJoint(v.joints[0]), EncodeJoint(v.joints[1])), PackPair(EncodeJoint(v.joints[2]), EncodeJoint(v.joints[3]))},
                    v.weights};
        }
    }

    MeshVertexData RhiGpuSkinning::QuantizeSource(const MeshVertexData &vertex)
    {
        auto result = vertex;
        const auto frame = EncodeFrame(vertex);
        for (std::size_t i = 0; i < 3; ++i)
        {
            result.normal[i] = DecodeSnorm16(frame[i]);
            result.tangent[i] = DecodeSnorm16(frame[3 + i]);
        }
        result.tangent[3] = DecodeSnorm16(frame[6]);
        for (auto &joint : result.joints)
            if (EncodeJoint(joint) == NoJoint) joint = -1;
        return result;
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

    std::shared_ptr<RhiGpuSkinningSource> RhiGpuSkinning::CreateSource(std::span<const MeshVertexData> vertices, SkinningLodLayout layout)
    {
        if (!m_device || vertices.empty() || vertices.size() > std::numeric_limits<std::uint32_t>::max() / sizeof(SourceVertex))
            throw std::invalid_argument("Invalid GPU skinning source");
        const auto vertexCount = static_cast<std::uint32_t>(vertices.size());
        if (layout.levelVertexCounts.empty())
            layout.levelVertexCounts = {vertexCount};
        if (!layout.sourceVertexOrder.empty() && layout.sourceVertexOrder.size() != vertices.size())
            throw std::invalid_argument("GPU skinning layout does not cover the source vertices");
        if (layout.levelVertexCounts.front() != vertexCount ||
            !std::is_sorted(layout.levelVertexCounts.rbegin(), layout.levelVertexCounts.rend()))
            throw std::invalid_argument("GPU skinning LOD prefixes must shrink from the full stream");
        std::vector<SourceVertex> packed;
        packed.reserve(vertices.size());
        for (std::uint32_t slot = 0; slot < vertexCount; ++slot)
        {
            const auto source = layout.sourceVertexOrder.empty() ? slot : layout.sourceVertexOrder[slot];
            if (source >= vertexCount) throw std::invalid_argument("GPU skinning layout references a missing vertex");
            packed.push_back(PackSource(vertices[source]));
        }
        auto result = std::make_shared<RhiGpuSkinningSource>();
        result->vertexCount = vertexCount;
        result->layout = std::move(layout);
        result->vertices = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {packed.size() * sizeof(SourceVertex),rhi::BufferUsage::Storage,"Skinning bind vertices",true}, Bytes(std::span(packed))));
        return result;
    }

    std::shared_ptr<RhiGpuSkinningState> RhiGpuSkinning::CreateState(std::shared_ptr<const RhiGpuSkinningSource> source)
    {
        if (!m_device || !source || !source->vertices) throw std::invalid_argument("Invalid GPU skinning state");
        auto result = std::make_shared<RhiGpuSkinningState>();
        result->source = std::move(source);
        return result;
    }

    void RhiGpuSkinning::Queue(RhiGpuSkinningState &state, std::span<const glm::mat4> palette, std::uint32_t firstVertex,
                               std::uint32_t endVertex, bool hasHistory)
    {
        if (!m_device || !state.source || palette.empty() || palette.size() > MaxJoints ||
            firstVertex > endVertex || endVertex > state.source->vertexCount)
            throw std::invalid_argument("Invalid GPU skinning palette or deformation range");
        if (state.pending)
        {
            firstVertex = std::min(firstVertex, state.firstVertex);
            endVertex = std::max(endVertex, state.endVertex);
            hasHistory = false;
        }
        state.paletteRows.resize(palette.size() * PaletteRowsPerJoint);
        for (std::size_t joint = 0; joint < palette.size(); ++joint)
        {
            const auto &matrix = palette[joint];
            for (int row = 0; row < 3; ++row)
                state.paletteRows[joint * PaletteRowsPerJoint + row] = {matrix[0][row], matrix[1][row], matrix[2][row], matrix[3][row]};
        }
        state.jointCount = static_cast<std::uint32_t>(palette.size());
        state.firstVertex = firstVertex;
        state.endVertex = endVertex;
        state.hasHistory = hasHistory;
        state.pending = true;
    }

    std::size_t RhiGpuSkinning::Record(std::span<const RhiGpuSkinningDispatch> dispatches)
    {
        // One palette transfer per frame: per-actor uploads each insert a
        // transfer barrier that serializes every preceding dispatch.
        m_paletteUpload.clear();
        m_batch.clear();
        for (const auto &dispatch : dispatches)
        {
            if (!dispatch.state || !dispatch.state->pending) continue;
            dispatch.state->pending = false;
            m_batch.push_back(dispatch);
            m_paletteUpload.insert(m_paletteUpload.end(), dispatch.state->paletteRows.begin(), dispatch.state->paletteRows.end());
        }
        if (m_batch.empty()) return 0;
        if (m_paletteUpload.size() > m_paletteCapacity)
        {
            m_paletteCapacity = std::max(m_paletteUpload.size(), m_paletteCapacity * 2);
            m_palettes = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {m_paletteCapacity * sizeof(glm::vec4),rhi::BufferUsage::Storage,"Skinning bone palettes"}));
        }
        m_device->UpdateBuffer(m_palettes.Get(), 0, Bytes(std::span(m_paletteUpload)));

        auto &commands = m_device->GetImmediateContext();
        commands.BindPipeline(m_pipeline.Get());
        std::uint32_t firstRow = 0;
        for (const auto &dispatch : m_batch)
        {
            auto &state = *dispatch.state;
            const std::array<std::uint32_t,8> parameters{state.endVertex,state.jointCount,firstRow,state.hasHistory ? 1u : 0u,
                                                         state.firstVertex,0,0,0};
            firstRow += state.jointCount * PaletteRowsPerJoint;
            const auto vertexCount = state.endVertex - state.firstVertex;
            if (vertexCount == 0) continue;
            if (m_nextParameters == m_parameters.size())
                m_parameters.emplace_back(*m_device, m_device->CreateBuffer({sizeof(parameters),rhi::BufferUsage::Uniform,"Skinning parameters"}));
            const auto &parameterBuffer = m_parameters[m_nextParameters++];
            m_device->UpdateBuffer(parameterBuffer.Get(), 0, Bytes(std::span(parameters)));
            commands.BindUniformBuffer(0,parameterBuffer.Get());
            commands.BindStorageBuffer(1,state.source->vertices.Get());
            commands.BindStorageBuffer(2,m_palettes.Get());
            commands.BindStorageBuffer(3,dispatch.output);
            // 2D dispatch avoids Vulkan's minimum 65535 workgroup limit on X.
            const auto groups = (vertexCount + 63u) / 64u;
            commands.Dispatch(std::min(groups,65535u),(groups + 65534u) / 65535u,1);
        }
        return m_batch.size();
    }
}
