#pragma once
#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"
#include <algorithm>
#include <cstring>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    // Coalesce adjacent changed records. Clear the mirror when replacing the
    // GPU allocation; commit it only after every upload succeeds.
    inline bool UploadChangedRecords(rhi::IRenderDevice &device, rhi::BufferHandle buffer,
        std::span<const std::byte> bytes, std::size_t stride, std::vector<std::byte> &mirror)
    {
        if (!stride || bytes.size() % stride) throw std::invalid_argument("Invalid GPU record stride");
        bool changed = bytes.size() != mirror.size();
        const auto differs = [&](std::size_t offset) {
            return offset + stride > mirror.size() || std::memcmp(bytes.data() + offset, mirror.data() + offset, stride) != 0;
        };
        // Each backend update may insert transfer barriers. Prefer one bulk
        // upload over a large number of tiny scattered transfers.
        unsigned ranges = 0;
        bool inRange = false;
        for (std::size_t offset = 0; offset < bytes.size(); offset += stride)
        {
            const bool different = differs(offset);
            if (different && !inRange && ++ranges > 8)
            {
                device.UpdateBuffer(buffer, 0, bytes);
                mirror.assign(bytes.begin(), bytes.end());
                return true;
            }
            inRange = different;
        }
        for (std::size_t begin = 0; begin < bytes.size();)
        {
            if (!differs(begin)) { begin += stride; continue; }
            auto end = begin + stride;
            while (end < bytes.size() && differs(end)) end += stride;
            device.UpdateBuffer(buffer, begin, bytes.subspan(begin, end - begin));
            changed = true;
            begin = end;
        }
        if (changed) mirror.assign(bytes.begin(), bytes.end());
        return changed;
    }

    // A storage buffer of fixed-size records that grows as needed and uploads only the records that changed.
    class GpuRecordTable
    {
    public:
        explicit GpuRecordTable(const char *name) : m_name(name) {}

        // Returns true when any record changed.
        bool Upload(rhi::IRenderDevice &device, std::span<const std::byte> bytes, std::size_t stride)
        {
            if (m_capacity < std::max<std::size_t>(stride, bytes.size()))
            {
                m_capacity = std::max<std::size_t>(stride, bytes.size() * 2);
                m_buffer = rhi::Buffer(device, device.CreateBuffer({m_capacity, rhi::BufferUsage::Storage, m_name}));
                m_uploaded.clear();
            }
            return UploadChangedRecords(device, m_buffer.Get(), bytes, stride, m_uploaded);
        }
        [[nodiscard]] rhi::BufferHandle Get() const noexcept { return m_buffer.Get(); }
        [[nodiscard]] std::size_t MemoryBytes() const noexcept { return m_capacity + m_uploaded.size(); }

    private:
        const char *m_name;
        rhi::Buffer m_buffer;
        std::vector<std::byte> m_uploaded;
        std::size_t m_capacity = 0;
    };
}
