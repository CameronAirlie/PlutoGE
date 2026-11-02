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
        m_device = nullptr;
    }

    bool RayTracingScene::Traceable(const BasicDraw &draw) noexcept
    {
        return draw.mesh && draw.mesh->IsValid() && draw.mesh->HasStaticGpuVertices() && draw.contributesToGi &&
               draw.surfaceType != 1 && draw.alphaMode != 2;
    }

    bool RayTracingScene::Update(rhi::ICommandContext &commands, std::span<const BasicDraw> draws)
    {
        if (!Available())
            return false;
        ++m_frame;
        m_instances.clear();
        m_records.clear();
        std::size_t built = 0;
        for (const auto &draw : draws)
        {
            if (!Traceable(draw))
                continue;
            const auto [firstIndex, indexCount] = IndexRange(draw);
            if (indexCount < 3)
                continue;
            const auto &mesh = *draw.mesh;
            const Key key{Pack(mesh.VertexBuffer()), Pack(mesh.IndexBuffer()), mesh.BaseVertex(),
                          mesh.BaseIndex() + firstIndex, indexCount, mesh.GetRevision()};
            auto found = m_bottomLevels.find(key);
            if (found == m_bottomLevels.end())
            {
                if (built == BuildBudget)
                    continue;
                rhi::BottomLevelAccelerationStructureDescriptor descriptor;
                descriptor.geometries.push_back({
                    .vertices = mesh.VertexBuffer(),
                    .vertexOffset = std::size_t(mesh.BaseVertex()) * sizeof(BasicVertex),
                    .vertexStride = static_cast<std::uint32_t>(sizeof(BasicVertex)),
                    .vertexCount = static_cast<std::uint32_t>(mesh.VertexCount()),
                    .indices = mesh.IndexBuffer(),
                    .indexOffset = std::size_t(mesh.BaseIndex() + firstIndex) * sizeof(std::uint32_t),
                    .indexCount = indexCount - indexCount % 3,
                });
                descriptor.debugName = "Ray-traced mesh";
                const auto handle = m_device->CreateBottomLevelAccelerationStructure(descriptor);
                if (!handle)
                    continue;
                commands.BuildBottomLevelAccelerationStructure(handle);
                ++built;
                found = m_bottomLevels.emplace(key, BottomLevel{rhi::AccelerationStructure(*m_device, handle), 0}).first;
            }
            found->second.lastUsed = m_frame;
            const RayTracedInstance record{
                glm::vec4(glm::vec3(draw.baseColor) * (1.0f - std::clamp(draw.metallic, 0.0f, 1.0f)), 1.0f),
                glm::vec4(glm::max(draw.emission, glm::vec3(0.0f)), 0.0f)};
            const auto place = [&](const glm::mat4 &model)
            {
                m_instances.push_back({found->second.structure.Get(), RowMajorTransform(model),
                                       static_cast<std::uint32_t>(m_records.size()), 0xFF});
                m_records.push_back(record);
            };
            if (draw.instanceModels && !draw.instanceModels->empty())
                for (const auto &model : *draw.instanceModels)
                    place(model);
            else
                place(draw.model);
        }
        std::erase_if(m_bottomLevels, [&](const auto &entry) { return entry.second.lastUsed + RetainFrames < m_frame; });
        m_instanceCount = m_instances.size();
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
            m_recordCapacity = m_topLevelCapacity;
            m_instanceRecords = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {m_recordCapacity * sizeof(RayTracedInstance), rhi::BufferUsage::Storage, "Ray-traced instance records"}));
        }
        if (!m_topLevel || !m_instanceRecords)
            return false;
        m_device->UpdateBuffer(m_instanceRecords.Get(), 0, std::as_bytes(std::span(m_records)));
        commands.BuildTopLevelAccelerationStructure(m_topLevel.Get(), m_instances);
        return true;
    }
}
