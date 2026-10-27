#include "GpuDrivenOpaque.h"

#include "PlutoGE/render/BasicRenderer.h"

#include <algorithm>
#include <cstring>
#include <tuple>

namespace PlutoGE::render
{
    namespace
    {
        // Matches BindlessMaterials.slang.
        constexpr std::uint32_t HasNormalTexture = 1, HasMetallicTexture = 2, HasRoughnessTexture = 4,
                                FlipNormalY = 8, TwoSided = 16;
        constexpr std::uint32_t NotTested = ~std::uint32_t{0};
        constexpr std::size_t CommandWords = 5;

        std::uint64_t Pack(rhi::TextureHandle texture)
        {
            return (std::uint64_t{texture.generation} << 32) | texture.index;
        }

        template <class T>
        auto Bytes(std::span<const T> values) { return std::as_bytes(values); }

        // Hashes whole 64-bit words (`size` is a multiple of eight: records and handle arrays).
        std::size_t HashWords(std::size_t hash, const void *data, std::size_t size)
        {
            const auto *bytes = static_cast<const std::byte *>(data);
            for (std::size_t offset = 0; offset + sizeof(std::uint64_t) <= size; offset += sizeof(std::uint64_t))
            {
                std::uint64_t word;
                std::memcpy(&word, bytes + offset, sizeof(word));
                hash = (hash ^ word) * 0x9e3779b97f4a7c15ull;
                hash ^= hash >> 29;
            }
            return hash;
        }

        // The standard-material values BasicRenderer's bound path uploads for `draw`, without textures.
        BindlessMaterialRecord MaterialValues(const BasicDraw &draw)
        {
            BindlessMaterialRecord record;
            record.baseColor = draw.baseColor;
            record.uvScaleMetallicRoughness = {draw.uvScale, draw.metallic, draw.roughness};
            record.emissionAlphaCutoff = {draw.emission, draw.alphaCutoff};
            record.flags = {draw.alphaMode,
                            (draw.normalTexture ? HasNormalTexture : 0u) | (draw.metallicTexture ? HasMetallicTexture : 0u) |
                                (draw.roughnessTexture ? HasRoughnessTexture : 0u) | (draw.flipNormalY ? FlipNormalY : 0u) |
                                (draw.twoSided ? TwoSided : 0u),
                            draw.metallicChannel, draw.roughnessChannel};
            record.subsurfaceColorStrength = {glm::max(draw.subsurfaceColor, glm::vec3(0.0f)), std::clamp(draw.subsurface, 0.0f, 1.0f)};
            record.subsurfaceRadiusPadding = {std::max(draw.subsurfaceRadius, 0.001f), 0.0f, 0.0f, 0.0f};
            record.glassParameters = {float(draw.surfaceType), std::clamp(draw.transmission, 0.0f, 1.0f),
                                      std::clamp(draw.ior, 1.0f, 3.0f), std::max(draw.thickness, 0.0f)};
            record.attenuationColorDistance = {glm::clamp(draw.attenuationColor, glm::vec3(0.0001f), glm::vec3(1.0f)),
                                               std::max(draw.attenuationDistance, 0.0001f)};
            record.emissionParameters = {draw.emissionTexture ? 1u : 0u, draw.emissionTexCoord == 1 ? 1u : 0u,
                                         draw.emissionChannelMask ? 1u : 0u, 0u};
            record.emissionChannels = draw.emissionChannels;
            return record;
        }
    }

    void BindlessMaterialTable::Reset(rhi::IRenderDevice &device, const Fallbacks &fallbacks)
    {
        Release(device);
        m_fallbacks = fallbacks;
    }

    std::uint32_t BindlessMaterialTable::AcquireTexture(rhi::IRenderDevice &device, rhi::TextureHandle texture)
    {
        auto &entry = m_textures[Pack(texture)];
        if (entry.references == 0)
        {
            entry.index = device.RegisterBindlessTexture(texture, m_fallbacks.sampler);
            if (entry.index == rhi::InvalidBindlessIndex)
            {
                m_textures.erase(Pack(texture));
                return rhi::InvalidBindlessIndex;
            }
        }
        ++entry.references;
        return entry.index;
    }

    void BindlessMaterialTable::ReleaseTexture(rhi::IRenderDevice &device, rhi::TextureHandle texture)
    {
        const auto found = m_textures.find(Pack(texture));
        if (found == m_textures.end() || --found->second.references != 0)
            return;
        device.ReleaseBindlessTexture(found->second.index);
        m_textures.erase(found);
    }

    std::uint32_t BindlessMaterialTable::Acquire(rhi::IRenderDevice &device, const BasicDraw &draw)
    {
        auto record = MaterialValues(draw);
        const std::array<rhi::TextureHandle, 5> textures{
            draw.baseColorTexture ? draw.baseColorTexture : m_fallbacks.baseColor,
            draw.normalTexture ? draw.normalTexture : m_fallbacks.normal,
            draw.metallicTexture ? draw.metallicTexture : m_fallbacks.data,
            draw.roughnessTexture ? draw.roughnessTexture : m_fallbacks.data,
            draw.emissionTexture ? draw.emissionTexture : m_fallbacks.baseColor};
        // Consecutive draws often share a material (submeshes, repeated props).
        if (m_lastSlot != rhi::InvalidBindlessIndex && m_lastTextures == textures &&
            std::memcmp(&m_lastValues, &record, sizeof(record)) == 0)
        {
            m_entries[m_lastSlot].lastUsed = m_frame;
            return m_lastSlot;
        }
        auto key = HashWords(14695981039346656037ull, &record, sizeof(record));
        key = HashWords(key, textures.data(), sizeof(textures));
        const auto remember = [&](std::uint32_t slot)
        {
            m_lastSlot = slot;
            m_lastValues = record;
            m_lastTextures = textures;
            return slot;
        };
        const auto [first, last] = m_byKey.equal_range(key);
        for (auto found = first; found != last; ++found)
        {
            auto &entry = m_entries[found->second];
            auto values = entry.record;
            values.textures = {};
            values.emissionTexture = {};
            if (entry.textures == textures && std::memcmp(&values, &record, sizeof(record)) == 0)
            {
                entry.lastUsed = m_frame;
                return remember(found->second);
            }
        }
        std::array<std::uint32_t, 5> indices{};
        for (std::size_t texture = 0; texture < textures.size(); ++texture)
        {
            indices[texture] = AcquireTexture(device, textures[texture]);
            if (indices[texture] == rhi::InvalidBindlessIndex)
            {
                for (std::size_t acquired = 0; acquired < texture; ++acquired)
                    ReleaseTexture(device, textures[acquired]);
                return rhi::InvalidBindlessIndex;
            }
        }
        const auto values = record;
        record.textures = {indices[0], indices[1], indices[2], indices[3]};
        record.emissionTexture = {indices[4], 0u, 0u, 0u};
        std::uint32_t slot;
        if (!m_free.empty())
        {
            slot = m_free.back();
            m_free.pop_back();
        }
        else
        {
            slot = static_cast<std::uint32_t>(m_entries.size());
            m_entries.emplace_back();
        }
        m_entries[slot] = {record, textures, key, m_frame, true};
        m_byKey.emplace(key, slot);
        record = values;
        return remember(slot);
    }

    void BindlessMaterialTable::Evict(rhi::IRenderDevice &device, std::uint32_t slot)
    {
        auto &entry = m_entries[slot];
        const auto [first, last] = m_byKey.equal_range(entry.key);
        for (auto found = first; found != last; ++found)
            if (found->second == slot)
            {
                m_byKey.erase(found);
                break;
            }
        for (const auto texture : entry.textures)
            ReleaseTexture(device, texture);
        entry = {};
        if (m_lastSlot == slot)
            m_lastSlot = rhi::InvalidBindlessIndex;
        m_free.push_back(slot);
    }

    void BindlessMaterialTable::EndFrame(rhi::IRenderDevice &device)
    {
        // Records are written in stream order, so frames already recorded keep reading the values they drew with.
        for (std::uint32_t slot = 0; slot < m_entries.size(); ++slot)
            if (m_entries[slot].live && m_entries[slot].lastUsed + RetainFrames < m_frame)
                Evict(device, slot);
        m_upload.resize(std::max<std::size_t>(m_entries.size(), 1));
        for (std::size_t slot = 0; slot < m_entries.size(); ++slot)
            m_upload[slot] = m_entries[slot].record;
        m_records.Upload(device, Bytes(std::span<const BindlessMaterialRecord>(m_upload)), sizeof(BindlessMaterialRecord));
        ++m_frame;
    }

    void BindlessMaterialTable::Release(rhi::IRenderDevice &device)
    {
        for (std::uint32_t slot = 0; slot < m_entries.size(); ++slot)
            if (m_entries[slot].live)
                Evict(device, slot);
        m_entries.clear();
        m_free.clear();
        m_byKey.clear();
        m_textures.clear();
    }

    void GpuDrivenOpaque::Initialize(rhi::IRenderDevice &device, const rhi::ComputePipelineDescriptor::ShaderCode &visibility,
                                     const BindlessMaterialTable::Fallbacks &fallbacks)
    {
        using namespace rhi;
        m_maxBatch = device.GetImmediateContext().MaxIndexedIndirectBatchSize();
        if (device.BindlessTextureCapacity() == 0 || m_maxBatch == 0 ||
            !device.GetImmediateContext().SupportsFirstInstance() || (visibility.spirv.empty() && visibility.glsl.empty()))
            return;
        m_visibilityPipeline = GraphicsPipeline(device, device.CreateComputePipeline(
            {visibility,
             {{0, 0, 0, ResourceBindingType::UniformBuffer, ShaderStageMask::Compute},
              {1, 0, 1, ResourceBindingType::StorageBuffer, ShaderStageMask::Compute},
              {2, 0, 2, ResourceBindingType::StorageBuffer, ShaderStageMask::Compute},
              {3, 0, 3, ResourceBindingType::StorageBuffer, ShaderStageMask::Compute}},
             "GPU-driven visibility"}));
        if (!m_visibilityPipeline)
            return;
        m_visibilityParameters = Buffer(device, device.CreateBuffer({sizeof(glm::uvec4), BufferUsage::Uniform, "GPU-driven visibility parameters"}));
        m_noOcclusion = Buffer(device, device.CreateBuffer({CommandWords * sizeof(std::uint32_t), BufferUsage::Storage,
                                                            "GPU-driven visibility without occlusion"}));
        m_materials.Reset(device, fallbacks);
    }

    void GpuDrivenOpaque::Shutdown(rhi::IRenderDevice &device)
    {
        m_materials.Release(device);
        m_visibilityPipeline.Reset();
    }

    bool GpuDrivenOpaque::Eligible(const BasicDraw &draw) noexcept
    {
        return draw.mesh && draw.mesh->IsValid() && !draw.shaderGraphProgram && !draw.terrainLayers &&
               draw.surfaceType == 0 && draw.alphaMode == 0 && !draw.outlinePass &&
               (!draw.instanceModels || draw.instanceModels->size() == 1);
    }

    void GpuDrivenOpaque::Prepare(rhi::IRenderDevice &device, std::span<const Source> sources, std::size_t drawCount)
    {
        m_stats = {};
        m_handled.assign(drawCount, false);
        m_instanceRecords.clear();
        m_commandWords.clear();
        m_occlusionIndices.clear();
        m_buckets.clear();
        if (!Available())
            return;
        struct Entry
        {
            const Source *source;
            std::uint32_t material, indexCount, firstIndex;
            std::tuple<std::uint32_t, std::uint32_t, std::uint32_t> bucket; // pipeline, vertices, indices
        };
        std::vector<Entry> entries;
        entries.reserve(sources.size());
        for (const auto &source : sources)
        {
            const auto &draw = *source.draw;
            const auto meshIndices = draw.mesh->GetIndexCount();
            const auto available = draw.firstIndex < meshIndices ? meshIndices - draw.firstIndex : 0u;
            const auto count = std::min(draw.indexCount == 0 ? available : draw.indexCount, available);
            if (count == 0 || !source.pipeline)
                continue;
            const auto material = m_materials.Acquire(device, draw);
            if (material == rhi::InvalidBindlessIndex)
                continue;
            // Absolute first indices, so draws of meshes sharing a geometry-arena page share one binding.
            entries.push_back({&source, material, count, draw.mesh->BaseIndex() + draw.firstIndex,
                               {source.pipeline.index, draw.mesh->VertexBuffer().index, draw.mesh->IndexBuffer().index}});
            m_stats.triangles += count / 3;
        }
        // One bucket per pipeline and geometry binding; order within a bucket keeps the draw order.
        std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.bucket < b.bucket; });
        for (const auto &entry : entries)
        {
            const auto &draw = *entry.source->draw;
            const auto command = static_cast<std::uint32_t>(m_instanceRecords.size());
            if (m_buckets.empty() || m_buckets.back().pipeline != entry.source->pipeline ||
                m_buckets.back().vertices != draw.mesh->VertexBuffer() || m_buckets.back().indices != draw.mesh->IndexBuffer())
                m_buckets.push_back({entry.source->pipeline, draw.mesh->VertexBuffer(), draw.mesh->IndexBuffer(), command, 0});
            ++m_buckets.back().commandCount;
            m_instanceRecords.push_back({entry.source->model, entry.source->previousModel, entry.source->debugParameters,
                                         glm::uvec4(entry.material, 0u, 0u, 0u)});
            // The instance count comes from RecordVisibility.
            m_commandWords.insert(m_commandWords.end(), {entry.indexCount, 1u, entry.firstIndex, 0u, command});
            m_occlusionIndices.push_back(static_cast<std::uint32_t>(entry.source->drawIndex));
            m_handled[entry.source->drawIndex] = true;
        }
        m_materials.EndFrame(device);
        m_stats.draws = m_instanceRecords.size();
        m_stats.materials = m_materials.Size();
        if (m_instanceRecords.empty())
            return;
        m_instances.Upload(device, Bytes(std::span<const BindlessInstanceRecord>(m_instanceRecords)), sizeof(BindlessInstanceRecord));
        m_commands.Upload(device, Bytes(std::span<const std::uint32_t>(m_commandWords)), CommandWords * sizeof(std::uint32_t));
        m_occlusionDraws.Upload(device, Bytes(std::span<const std::uint32_t>(m_occlusionIndices)), sizeof(std::uint32_t));
    }

    void GpuDrivenOpaque::RecordVisibility(rhi::IRenderDevice &device, rhi::ICommandContext &commands,
                                           rhi::BufferHandle occlusionCommands)
    {
        using rhi::BarrierAccess;
        if (m_instanceRecords.empty())
            return;
        const auto count = static_cast<std::uint32_t>(m_instanceRecords.size());
        const glm::uvec4 parameters{count, occlusionCommands ? 1u : 0u, 0u, 0u};
        device.UpdateBuffer(m_visibilityParameters.Get(), 0, std::as_bytes(std::span(&parameters, 1)));
        commands.BindPipeline(m_visibilityPipeline.Get());
        commands.BindUniformBuffer(0, m_visibilityParameters.Get());
        commands.BindStorageBuffer(1, m_occlusionDraws.Get());
        commands.BindStorageBuffer(2, occlusionCommands ? occlusionCommands : m_noOcclusion.Get());
        commands.BindStorageBuffer(3, m_commands.Get());
        // Earlier multi-draws read these commands; occlusion testing wrote its results for indirect reads only.
        commands.PipelineBarrier(BarrierAccess::IndirectCommandRead | BarrierAccess::ComputeShaderWrite,
                                 BarrierAccess::Compute);
        commands.Dispatch((count + 63) / 64, 1, 1);
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite, BarrierAccess::IndirectCommandRead);
    }

    std::size_t GpuDrivenOpaque::Draw(rhi::ICommandContext &commands)
    {
        if (m_buckets.empty())
            return 0;
        commands.BindStorageBuffer(InstancesSlot, m_instances.Get());
        commands.BindStorageBuffer(MaterialsSlot, m_materials.Buffer());
        std::size_t calls = 0;
        rhi::PipelineHandle boundPipeline;
        for (const auto &bucket : m_buckets)
        {
            if (bucket.pipeline != boundPipeline)
            {
                commands.BindPipeline(bucket.pipeline);
                boundPipeline = bucket.pipeline;
            }
            commands.BindVertexBuffer(bucket.vertices);
            commands.BindIndexBuffer(bucket.indices);
            for (std::uint32_t first = 0; first < bucket.commandCount; first += m_maxBatch)
            {
                const auto count = std::min(m_maxBatch, bucket.commandCount - first);
                commands.DrawIndexedIndirectBatch(m_commands.Get(), (bucket.firstCommand + first) * CommandWords * sizeof(std::uint32_t), count);
                ++calls;
            }
        }
        m_stats.batches = calls;
        return calls;
    }
}
