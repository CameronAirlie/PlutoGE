#include "RayTracingScene.h"

#include <algorithm>
#include <bit>

namespace PlutoGE::render
{
    namespace
    {
        std::uint64_t Pack(rhi::BufferHandle buffer)
        {
            return (std::uint64_t{buffer.generation} << 32) | buffer.index;
        }

        // The object-to-world matrix's top three rows, row-major.
        std::array<float, 12> RowMajorTransform(const glm::mat4 &model)
        {
            std::array<float, 12> rows{};
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 4; ++column)
                    rows[row * 4 + column] = model[column][row];
            return rows;
        }

        // Hits are not textured: the material's means over its textures where known, else its factors.
        RayTracedInstance SurfaceOf(const BasicDraw &draw)
        {
            const glm::vec3 diffuse = draw.meanDiffuse.x >= 0.0f
                ? draw.meanDiffuse : glm::vec3(draw.baseColor) * (1.0f - std::clamp(draw.metallic, 0.0f, 1.0f));
            const glm::vec3 emission = draw.meanEmission.x >= 0.0f ? draw.meanEmission : draw.emission;
            return {glm::vec4(glm::clamp(diffuse, glm::vec3(0.0f), glm::vec3(1.0f)), 1.0f),
                    glm::vec4(glm::max(emission, glm::vec3(0.0f)), 0.0f)};
        }

        // The draw's index range, as the geometry pass draws it.
        std::pair<std::uint32_t, std::uint32_t> IndexRange(const BasicDraw &draw)
        {
            const auto total = draw.mesh->GetIndexCount();
            const auto first = std::min(draw.firstIndex, total);
            const auto available = total - first;
            return {first, draw.indexCount ? std::min(draw.indexCount, available) : available};
        }
    }

    bool RayTracingScene::Initialize(rhi::IRenderDevice &device)
    {
        Shutdown();
        if (!device.GetRayTracingSupport().supported)
            return false;
        m_device = &device;
        return true;
    }

    void RayTracingScene::Shutdown()
    {
        m_bottomLevels.clear();
        m_topLevel.Reset();
        m_instanceRecords.Reset();
        m_topLevelCapacity = 0;
        m_recordCapacity = 0;
        m_instanceCount = 0;
        m_skippedInstanceCount = 0;
        m_outOfRangeInstanceCount = 0;
        m_builtSignature.reset();
        m_rebuilt = false;
        m_device = nullptr;
    }

    bool RayTracingScene::Traceable(const BasicDraw &draw) noexcept
    {
        return draw.mesh && draw.mesh->IsValid() && draw.mesh->HasStaticGpuVertices() && draw.contributesToGi &&
               draw.surfaceType != 1 && draw.alphaMode != 2;
    }

    RayTracingScene::Selection RayTracingScene::Select(const BasicDraw &draw) const noexcept
    {
        if (draw.maxInstanceRadius >= 0.0f && draw.maxInstanceRadius < m_settings.minimumInstanceRadius)
            return Selection::TooSmall;
        if (m_settings.range > 0.0f && draw.shadowBoundsRadius >= 0.0f)
        {
            const float reach = m_settings.range + draw.shadowBoundsRadius;
            const glm::vec3 offset = draw.shadowBoundsCenter - m_settings.center;
            if (glm::dot(offset, offset) > reach * reach)
                return Selection::OutOfRange;
        }
        return Selection::Traced;
    }

    std::optional<std::uint64_t> RayTracingScene::InputSignature(std::span<const BasicDraw> draws) const
    {
        // FNV-1a over a few words per draw: tens of thousands of draws cost well under a millisecond.
        std::uint64_t hash = 14695981039346656037ull;
        const auto mix = [&](std::uint64_t value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        mix(std::bit_cast<std::uint32_t>(m_settings.minimumInstanceRadius));
        mix(std::bit_cast<std::uint32_t>(m_settings.range));
        for (int axis = 0; axis < 3; ++axis)
            mix(std::bit_cast<std::uint32_t>(m_settings.center[axis]));
        for (const auto &draw : draws)
        {
            if (!Traceable(draw))
                continue;
            // A traceable draw left out still counts: crossing a threshold must rebuild.
            if (!draw.preparationRevision)
                return std::nullopt;
            mix(draw.preparationRevision);
            mix(draw.mesh->GetRevision());
            // The hit surface too: texture means arrive once a texture's pixels are read, without a new packet.
            const auto surface = SurfaceOf(draw);
            for (int channel = 0; channel < 3; ++channel)
            {
                mix(std::bit_cast<std::uint32_t>(surface.albedo[channel]));
                mix(std::bit_cast<std::uint32_t>(surface.emission[channel]));
            }
        }
        return hash;
    }

    bool RayTracingScene::Update(rhi::ICommandContext &commands, std::span<const BasicDraw> draws)
    {
        if (!Available())
            return false;
        const auto signature = InputSignature(draws);
        m_rebuilt = false;
        if (signature && signature == m_builtSignature && m_topLevel && m_instanceCount != 0)
            return true;
        m_rebuilt = true;
        ++m_frame;
        m_instances.clear();
        m_records.clear();
        m_skippedInstanceCount = 0;
        m_outOfRangeInstanceCount = 0;
        std::size_t built = 0;
        bool complete = true;
        for (const auto &draw : draws)
        {
            if (!Traceable(draw))
                continue;
            const std::size_t instanceCount = draw.instanceModels && !draw.instanceModels->empty()
                ? draw.instanceModels->size() : 1;
            if (const auto selection = Select(draw); selection != Selection::Traced)
            {
                (selection == Selection::TooSmall ? m_skippedInstanceCount : m_outOfRangeInstanceCount) += instanceCount;
                continue;
            }
            const auto [firstIndex, indexCount] = IndexRange(draw);
            if (indexCount < 3)
                continue;
            const auto &mesh = *draw.mesh;
            const Key key{Pack(mesh.VertexBuffer()), Pack(mesh.IndexBuffer()), mesh.BaseVertex(),
                          mesh.BaseIndex() + firstIndex, indexCount, mesh.GetRevision()};
            auto found = m_bottomLevels.find(key);
            if (found == m_bottomLevels.end())
            {
                // A deferred or failed level leaves the top level incomplete: the next Update rebuilds it.
                if (built == BuildBudget)
                {
                    complete = false;
                    continue;
                }
                // Indices are stored rebased by the mesh's first vertex (BasicMesh::VertexBuffer), so they address the
                // buffer from its start, as draws do with a vertex offset of zero; the highest is BaseVertex past the
                // mesh's own count.
                rhi::BottomLevelAccelerationStructureDescriptor descriptor;
                descriptor.geometries.push_back({
                    .vertices = mesh.VertexBuffer(),
                    .vertexOffset = 0,
                    .vertexStride = static_cast<std::uint32_t>(sizeof(BasicVertex)),
                    .vertexCount = static_cast<std::uint32_t>(mesh.BaseVertex() + mesh.VertexCount()),
                    .indices = mesh.IndexBuffer(),
                    .indexOffset = std::size_t(mesh.BaseIndex() + firstIndex) * sizeof(std::uint32_t),
                    .indexCount = indexCount - indexCount % 3,
                });
                descriptor.debugName = "Ray-traced mesh";
                const auto handle = m_device->CreateBottomLevelAccelerationStructure(descriptor);
                if (!handle)
                {
                    complete = false;
                    continue;
                }
                commands.BuildBottomLevelAccelerationStructure(handle);
                ++built;
                found = m_bottomLevels.emplace(key, BottomLevel{rhi::AccelerationStructure(*m_device, handle), 0}).first;
            }
            found->second.lastUsed = m_frame;
            const auto recordIndex = static_cast<std::uint32_t>(m_records.size());
            m_records.push_back(SurfaceOf(draw));
            const auto bottomLevel = found->second.structure.Get();
            if (draw.instanceModels && !draw.instanceModels->empty())
                for (const auto &model : *draw.instanceModels)
                    m_instances.push_back({bottomLevel, RowMajorTransform(model), recordIndex, 0xFF});
            else
                m_instances.push_back({bottomLevel, RowMajorTransform(draw.model), recordIndex, 0xFF});
        }
        std::erase_if(m_bottomLevels, [&](const auto &entry) { return entry.second.lastUsed + RetainFrames < m_frame; });
        m_instanceCount = m_instances.size();
        m_builtSignature = complete ? signature : std::nullopt;
        if (m_instances.empty())
            return false;
        if (m_instances.size() > m_topLevelCapacity)
        {
            m_topLevelCapacity = std::bit_ceil(std::max<std::uint32_t>(static_cast<std::uint32_t>(m_instances.size()), 1024u));
            m_topLevel = rhi::AccelerationStructure(*m_device, m_device->CreateTopLevelAccelerationStructure(
                {m_topLevelCapacity, "Ray-traced scene"}));
        }
        if (m_records.size() > m_recordCapacity)
        {
            m_recordCapacity = std::bit_ceil(std::max<std::size_t>(m_records.size(), 256u));
            m_instanceRecords = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {m_recordCapacity * sizeof(RayTracedInstance), rhi::BufferUsage::Storage, "Ray-traced instance records"}));
        }
        if (!m_topLevel || !m_instanceRecords)
        {
            m_builtSignature.reset();
            return false;
        }
        m_device->UpdateBuffer(m_instanceRecords.Get(), 0, std::as_bytes(std::span(m_records)));
        commands.BuildTopLevelAccelerationStructure(m_topLevel.Get(), m_instances);
        return true;
    }
}
