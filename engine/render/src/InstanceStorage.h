#pragma once

#include "PlutoGE/render/rhi/Resource.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PlutoGE::render
{
    // Instance records of instanced draws (InstanceRecord in BasicLit.slang: current and previous model matrix) in
    // storage buffers that persist across frames.
    //
    // Static instance arrays (foliage clusters, retained by their producers) are uploaded once and then only bound:
    // per frame a draw costs one small uniform with its first record, instead of copying every instance's matrices
    // into the per-frame uniform arena. Arrays are identified by their shared vectors, which an entry keeps alive so an
    // address is never reused while its records are resident. An array unused for a few frames is released; a page
    // whose records are all released is reused once no frame in flight can still read it.
    class InstanceStorage
    {
    public:
        struct Record
        {
            glm::mat4 model{1.0f};
            glm::mat4 previousModel{1.0f};
        };
        static_assert(sizeof(Record) == 128);

        struct Range
        {
            rhi::BufferHandle buffer;
            std::uint32_t first = 0;
            [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(buffer); }
        };

        // Records per storage page (8 MiB); one draw's array must fit in a page.
        static constexpr std::uint32_t PageRecords = 65536;
        // Frames an unused array stays resident, and a released page waits before reuse (exceeds frames in flight).
        static constexpr std::uint64_t RetainFrames = 4;

        explicit InstanceStorage(rhi::IRenderDevice &device) : m_device(device) {}

        void BeginFrame() { ++m_frame; m_uploadedRecords = 0; m_stagedIndices.clear(); m_bins.clear(); }
        // Uploads the array's records unless resident. Call outside rendering (uploads are recorded copies).
        // `previous` (same length) supplies last frame's matrices; without it each record repeats its model.
        // `retained` arrays are immutable (renderer-retained packets, BasicDraw::preparationRevision) and upload once;
        // others may change behind the same pointer, so they upload once per frame, in place.
        Range Prepare(const std::shared_ptr<const std::vector<glm::mat4>> &models,
                      const std::shared_ptr<const std::vector<glm::mat4>> &previous, bool retained);
        // The array's records, prepared this frame; empty when it was not.
        [[nodiscard]] Range Find(const std::vector<glm::mat4> *models, const std::vector<glm::mat4> *previous) const;
        // Releases arrays not prepared within RetainFrames.
        void EndFrame();

        // Per-instance LOD for one draw this frame: its instances' record indices grouped by LOD, in the frame's index
        // buffer. bins[lod] is that LOD's first index and count.
        struct LodBins
        {
            struct Bin { std::uint32_t first = 0, count = 0; };
            std::vector<Bin> bins;
        };
        // Stages a draw's binned instances (indices into its array) for this frame, keyed by its array and LOD chain.
        void StageBins(const Range &records, const void *models, const void *chain,
                       const std::vector<std::vector<std::uint32_t>> &instancesByLod);
        // The bins staged this frame for a draw's array and LOD chain, or null.
        [[nodiscard]] const LodBins *FindBins(const void *models, const void *chain) const;
        // Uploads the frame's staged indices. Call once after staging, outside rendering.
        void UploadBins();
        [[nodiscard]] rhi::BufferHandle IndexBuffer() const noexcept { return m_indexBuffer.Get(); }

        [[nodiscard]] std::size_t UploadedRecords() const noexcept { return m_uploadedRecords; }
        [[nodiscard]] std::size_t ResidentRecords() const noexcept { return m_residentRecords; }

    private:
        struct Key
        {
            const std::vector<glm::mat4> *models = nullptr;
            const std::vector<glm::mat4> *previous = nullptr;
            bool operator==(const Key &) const = default;
        };
        struct KeyHash
        {
            std::size_t operator()(const Key &key) const noexcept
            {
                return std::hash<const void *>{}(key.models) ^ (std::hash<const void *>{}(key.previous) * 31u);
            }
        };
        struct Entry
        {
            std::shared_ptr<const std::vector<glm::mat4>> models, previous;
            std::uint32_t page = 0, first = 0, count = 0;
            std::uint64_t lastFrame = 0;
            std::uint64_t uploadedFrame = 0;
            bool retained = false;
        };
        struct Page
        {
            rhi::Buffer buffer;
            std::uint32_t used = 0;     // bump allocation
            std::uint32_t live = 0;     // records of resident entries
            std::uint64_t emptiedFrame = 0;
        };

        bool Allocate(std::uint32_t count, std::uint32_t &page, std::uint32_t &first);

        rhi::IRenderDevice &m_device;
        rhi::Buffer m_indexBuffer;
        std::size_t m_indexCapacity = 0;
        std::vector<std::uint32_t> m_stagedIndices;
        std::vector<std::uint32_t> m_uploadedIndices; // the index buffer's contents, to skip unchanged uploads
        struct BinKeyHash
        {
            std::size_t operator()(const std::pair<const void *, const void *> &key) const noexcept
            {
                return std::hash<const void *>{}(key.first) ^ (std::hash<const void *>{}(key.second) * 31u);
            }
        };
        std::unordered_map<std::pair<const void *, const void *>, LodBins, BinKeyHash> m_bins;
        std::unordered_map<Key, Entry, KeyHash> m_entries;
        std::vector<Page> m_pages;
        std::uint64_t m_frame = 0;
        std::size_t m_uploadedRecords = 0;
        std::size_t m_residentRecords = 0;
    };
}
