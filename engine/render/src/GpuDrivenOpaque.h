#pragma once

#include "ChangedBufferRecords.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    struct BasicDraw;

    // Matches BindlessMaterials.slang's BindlessMaterial: every member is 16 bytes, so storage and uniform layouts
    // agree.
    struct alignas(16) BindlessMaterialRecord
    {
        glm::vec4 baseColor{1.0f};
        glm::vec4 uvScaleMetallicRoughness{1.0f, 1.0f, 0.0f, 1.0f};
        glm::vec4 emissionAlphaCutoff{0.0f, 0.0f, 0.0f, 0.5f};
        glm::uvec4 flags{0u}; // alpha mode, BindlessMaterialFlag bits, metallic channel, roughness channel
        glm::vec4 subsurfaceColorStrength{0.0f};
        glm::vec4 subsurfaceRadiusPadding{0.0f};
        glm::vec4 glassParameters{0.0f};
        glm::vec4 attenuationColorDistance{0.0f};
        glm::uvec4 emissionParameters{0u};
        std::array<glm::vec4, 3> emissionChannels{};
        glm::uvec4 textures{0u}; // bindless indices: base colour, normal, metallic, roughness
        glm::uvec4 emissionTexture{0u};
    };
    static_assert(sizeof(BindlessMaterialRecord) == 14 * 16);

    // Matches BindlessMaterials.slang's BindlessInstance.
    struct alignas(16) BindlessInstanceRecord
    {
        glm::mat4 model{1.0f};
        glm::mat4 previousModel{1.0f};
        glm::vec4 debugParameters{0.0f};
        glm::uvec4 material{0u};
    };
    static_assert(sizeof(BindlessInstanceRecord) == 160);

    // Textures standing in for a material's absent maps, and the sampler every material texture uses.
    struct BindlessMaterialFallbacks
    {
        rhi::TextureHandle baseColor, normal, data;
        rhi::SamplerHandle sampler;
    };

    // Standard materials as records in a storage buffer, their textures registered in the device's bindless table.
    // Records live while draws use them: one unused for `RetainFrames` frames is evicted and its textures' table
    // slots released. Record contents are validated, never identified by hash alone.
    class BindlessMaterialTable
    {
    public:
        using Fallbacks = BindlessMaterialFallbacks;
        static constexpr std::uint64_t RetainFrames = 8;

        void Reset(rhi::IRenderDevice &device, const Fallbacks &fallbacks);
        // The draw's record index, or rhi::InvalidBindlessIndex when a texture cannot enter the bindless table.
        [[nodiscard]] std::uint32_t Acquire(rhi::IRenderDevice &device, const BasicDraw &draw);
        // Uploads changed records and evicts records unused since `RetainFrames` frames ago.
        void EndFrame(rhi::IRenderDevice &device);
        void Release(rhi::IRenderDevice &device);
        [[nodiscard]] rhi::BufferHandle Buffer() const noexcept { return m_records.Get(); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_entries.size() - m_free.size(); }

    private:
        struct TextureEntry
        {
            std::uint32_t index = rhi::InvalidBindlessIndex;
            std::uint32_t references = 0;
        };
        struct Entry
        {
            BindlessMaterialRecord record; // texture members hold bindless indices
            std::array<rhi::TextureHandle, 5> textures{};
            std::size_t key = 0;
            std::uint64_t lastUsed = 0;
            bool live = false;
        };
        [[nodiscard]] std::uint32_t AcquireTexture(rhi::IRenderDevice &device, rhi::TextureHandle texture);
        void ReleaseTexture(rhi::IRenderDevice &device, rhi::TextureHandle texture);
        void Evict(rhi::IRenderDevice &device, std::uint32_t slot);

        Fallbacks m_fallbacks;
        std::vector<Entry> m_entries;
        std::vector<std::uint32_t> m_free;
        std::unordered_multimap<std::size_t, std::uint32_t> m_byKey;
        std::unordered_map<std::uint64_t, TextureEntry> m_textures; // by packed handle
        std::vector<BindlessMaterialRecord> m_upload;
        // The last record acquired, by its values without texture indices and its textures.
        std::uint32_t m_lastSlot = rhi::InvalidBindlessIndex;
        BindlessMaterialRecord m_lastValues;
        std::array<rhi::TextureHandle, 5> m_lastTextures{};
        GpuRecordTable m_records{"Bindless materials"};
        std::uint64_t m_frame = 0;
    };

    // GPU-driven opaque geometry (docs/renderer-modernisation.md, Phase 3). Standard opaque draws become instance and
    // material records in storage buffers and draw as one multi-draw-indirect call per pipeline and geometry
    // buffer pair, sampling material textures from the bindless table. A compute pass copies GPU occlusion results
    // into the commands. Every other draw keeps the bound-material path.
    class GpuDrivenOpaque
    {
    public:
        // Storage-buffer slots the bindless lit pipelines declare (BindlessMaterials.slang).
        static constexpr std::uint32_t InstancesSlot = 5;
        static constexpr std::uint32_t MaterialsSlot = 6;

        // One eligible draw, with the per-object values the bound path would upload for it.
        struct Source
        {
            const BasicDraw *draw = nullptr;
            std::size_t drawIndex = 0;  // in the frame's draw list (and GPU occlusion's commands)
            glm::mat4 model{1.0f}, previousModel{1.0f};
            glm::vec4 debugParameters{0.0f};
            rhi::PipelineHandle pipeline;
        };
        struct Stats
        {
            std::size_t draws = 0, batches = 0, materials = 0;
            std::uint64_t triangles = 0;
        };

        // Requires bindless textures, multi-draw indirect and the visibility shader; otherwise unavailable.
        void Initialize(rhi::IRenderDevice &device, const rhi::ComputePipelineDescriptor::ShaderCode &visibility,
                        const BindlessMaterialTable::Fallbacks &fallbacks);
        void Shutdown(rhi::IRenderDevice &device);
        // Re-registers materials, e.g. after the material sampler changed.
        void ResetMaterials(rhi::IRenderDevice &device, const BindlessMaterialFallbacks &fallbacks)
        {
            m_materials.Reset(device, fallbacks);
        }
        [[nodiscard]] bool Available() const noexcept { return static_cast<bool>(m_visibilityPipeline); }
        // Material eligibility: standard (no graph or terrain), opaque, one object. The draw also needs bindless
        // textures, which Prepare decides.
        [[nodiscard]] static bool Eligible(const BasicDraw &draw) noexcept;

        // Builds this frame's tables and buckets from `sources` (drawIndex ascending); draws whose textures cannot
        // be bindless stay with the bound path.
        void Prepare(rhi::IRenderDevice &device, std::span<const Source> sources, std::size_t drawCount);
        [[nodiscard]] bool Handles(std::size_t drawIndex) const noexcept
        {
            return drawIndex < m_handled.size() && m_handled[drawIndex];
        }
        // Outside rendering, after GPU occlusion testing: sets each command's instance count from
        // `occlusionCommands` (OcclusionCulling's per-draw commands), or to one without them.
        void RecordVisibility(rhi::IRenderDevice &device, rhi::ICommandContext &commands,
                              rhi::BufferHandle occlusionCommands);
        // Inside the geometry pass, with its frame and pass resources bound. Returns the multi-draw calls recorded.
        std::size_t Draw(rhi::ICommandContext &commands);
        [[nodiscard]] const Stats &FrameStats() const noexcept { return m_stats; }

    private:
        struct Bucket
        {
            rhi::PipelineHandle pipeline;
            rhi::BufferHandle vertices, indices;
            std::uint32_t firstCommand = 0, commandCount = 0;
        };

        BindlessMaterialTable m_materials;
        rhi::GraphicsPipeline m_visibilityPipeline;
        rhi::Buffer m_visibilityParameters, m_noOcclusion;
        GpuRecordTable m_instances{"GPU-driven instances"};
        GpuRecordTable m_commands{"GPU-driven indirect commands"};
        GpuRecordTable m_occlusionDraws{"GPU-driven occlusion draws"};
        std::vector<BindlessInstanceRecord> m_instanceRecords;
        std::vector<std::uint32_t> m_commandWords, m_occlusionIndices;
        std::vector<Bucket> m_buckets;
        std::vector<bool> m_handled;
        std::uint32_t m_maxBatch = 0;
        Stats m_stats;
    };
}
