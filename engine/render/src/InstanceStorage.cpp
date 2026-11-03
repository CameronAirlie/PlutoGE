#include "InstanceStorage.h"

#include <algorithm>
#include <cstring>
#include <span>

namespace PlutoGE::render
{
    bool InstanceStorage::Allocate(std::uint32_t count, std::uint32_t &page, std::uint32_t &first)
    {
        if (count == 0 || count > PageRecords)
            return false;
        for (std::uint32_t index = 0; index < m_pages.size(); ++index)
        {
            auto &candidate = m_pages[index];
            // A page whose records were all released is rewound once no frame in flight can still read it.
            if (candidate.live == 0 && candidate.used != 0 && m_frame > candidate.emptiedFrame + RetainFrames)
                candidate.used = 0;
            if (PageRecords - candidate.used >= count)
            {
                page = index;
                first = candidate.used;
                candidate.used += count;
                candidate.live += count;
                return true;
            }
        }
        Page created;
        created.buffer = rhi::Buffer(m_device, m_device.CreateBuffer(
            {static_cast<std::size_t>(PageRecords) * sizeof(Record), rhi::BufferUsage::Storage, "Instance records"}));
        if (!created.buffer)
            return false;
        created.used = count;
        created.live = count;
        page = static_cast<std::uint32_t>(m_pages.size());
        first = 0;
        m_pages.push_back(std::move(created));
        return true;
    }

    InstanceStorage::Range InstanceStorage::Prepare(const std::shared_ptr<const std::vector<glm::mat4>> &models,
                                                   const std::shared_ptr<const std::vector<glm::mat4>> &previous)
    {
        if (!models || models->empty())
            return {};
        const bool hasPrevious = previous && previous->size() == models->size();
        const Key key{models.get(), hasPrevious ? previous.get() : nullptr};
        if (const auto found = m_entries.find(key); found != m_entries.end())
        {
            found->second.lastFrame = m_frame;
            return {m_pages[found->second.page].buffer.Get(), found->second.first};
        }
        Entry entry{models, hasPrevious ? previous : nullptr, 0, 0, static_cast<std::uint32_t>(models->size()), m_frame};
        if (!Allocate(entry.count, entry.page, entry.first))
            return {};
        std::vector<Record> records(entry.count);
        for (std::size_t index = 0; index < records.size(); ++index)
            records[index] = {(*models)[index], hasPrevious ? (*previous)[index] : (*models)[index]};
        m_device.UpdateBuffer(m_pages[entry.page].buffer.Get(), static_cast<std::size_t>(entry.first) * sizeof(Record),
                              std::as_bytes(std::span(records)));
        m_uploadedRecords += records.size();
        m_residentRecords += records.size();
        const Range range{m_pages[entry.page].buffer.Get(), entry.first};
        m_entries.emplace(key, std::move(entry));
        return range;
    }

    InstanceStorage::Range InstanceStorage::Find(const std::vector<glm::mat4> *models,
                                                 const std::vector<glm::mat4> *previous) const
    {
        const auto found = m_entries.find({models, previous && models && previous->size() == models->size() ? previous : nullptr});
        if (found == m_entries.end() || found->second.lastFrame != m_frame)
            return {};
        return {m_pages[found->second.page].buffer.Get(), found->second.first};
    }

    void InstanceStorage::StageBins(const Range &records, const void *models, const void *chain,
                                    const std::vector<std::vector<std::uint32_t>> &instancesByLod)
    {
        LodBins &result = m_bins[{models, chain}];
        result.bins.resize(instancesByLod.size());
        for (std::size_t lod = 0; lod < instancesByLod.size(); ++lod)
        {
            result.bins[lod] = {static_cast<std::uint32_t>(m_stagedIndices.size()),
                                static_cast<std::uint32_t>(instancesByLod[lod].size())};
            for (const auto instance : instancesByLod[lod])
                m_stagedIndices.push_back(records.first + instance);
        }
    }

    const InstanceStorage::LodBins *InstanceStorage::FindBins(const void *models, const void *chain) const
    {
        const auto found = m_bins.find({models, chain});
        return found != m_bins.end() ? &found->second : nullptr;
    }

    void InstanceStorage::UploadBins()
    {
        if (m_stagedIndices.empty())
            return;
        if (m_indexCapacity < m_stagedIndices.size())
        {
            // Grow geometrically; the GPU copy that fills it is ordered after earlier frames' reads.
            m_indexCapacity = std::max<std::size_t>(m_stagedIndices.size() + m_stagedIndices.size() / 2, 65536);
            m_indexBuffer = rhi::Buffer(m_device, m_device.CreateBuffer(
                {m_indexCapacity * sizeof(std::uint32_t), rhi::BufferUsage::Storage, "Instance LOD bins"}));
        }
        m_device.UpdateBuffer(m_indexBuffer.Get(), 0, std::as_bytes(std::span(m_stagedIndices)));
    }

    void InstanceStorage::EndFrame()
    {
        for (auto entry = m_entries.begin(); entry != m_entries.end();)
        {
            if (m_frame - entry->second.lastFrame >= RetainFrames)
            {
                auto &page = m_pages[entry->second.page];
                page.live -= entry->second.count;
                if (page.live == 0)
                    page.emptiedFrame = m_frame;
                m_residentRecords -= entry->second.count;
                entry = m_entries.erase(entry);
            }
            else
                ++entry;
        }
    }
}
