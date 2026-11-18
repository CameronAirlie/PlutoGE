#include "PlutoGE/scene/volume/VolumeFile.h"

#include "PlutoGE/scene/volume/TerrainVolume.h"

#include <bit>
#include <cstring>

namespace PlutoGE::scene::volume::VolumeFile
{
    namespace
    {
        constexpr std::uint8_t Magic[4] = {'P', 'L', 'V', 'O'};
        // A chunk header: coordinate and payload size.
        constexpr std::size_t ChunkHeaderSize = 4 * 4;
        // More samples per side than a valid grid allows are refused before anything is allocated.
        constexpr int MaxChunkVoxels = 128;

        class Writer
        {
        public:
            explicit Writer(std::vector<std::uint8_t> &out) : m_out(out) {}
            void U32(std::uint32_t value)
            {
                for (int shift = 0; shift < 32; shift += 8)
                    m_out.push_back(static_cast<std::uint8_t>(value >> shift));
            }
            void I32(std::int32_t value) { U32(static_cast<std::uint32_t>(value)); }
            void F32(float value) { U32(std::bit_cast<std::uint32_t>(value)); }
            void Bytes(const void *data, std::size_t size)
            {
                const auto *bytes = static_cast<const std::uint8_t *>(data);
                m_out.insert(m_out.end(), bytes, bytes + size);
            }

        private:
            std::vector<std::uint8_t> &m_out;
        };

        class Reader
        {
        public:
            explicit Reader(std::span<const std::uint8_t> bytes) : m_bytes(bytes) {}
            [[nodiscard]] bool Has(std::size_t size) const { return m_bytes.size() - m_position >= size; }
            bool U32(std::uint32_t &value)
            {
                if (!Has(4))
                    return false;
                value = 0;
                for (int index = 0; index < 4; ++index)
                    value |= static_cast<std::uint32_t>(m_bytes[m_position++]) << (index * 8);
                return true;
            }
            bool I32(std::int32_t &value)
            {
                std::uint32_t raw = 0;
                if (!U32(raw))
                    return false;
                value = static_cast<std::int32_t>(raw);
                return true;
            }
            bool F32(float &value)
            {
                std::uint32_t raw = 0;
                if (!U32(raw))
                    return false;
                value = std::bit_cast<float>(raw);
                return true;
            }
            std::span<const std::uint8_t> Take(std::size_t size)
            {
                if (!Has(size))
                    return {};
                const auto taken = m_bytes.subspan(m_position, size);
                m_position += size;
                return taken;
            }
            [[nodiscard]] bool AtEnd() const { return m_position == m_bytes.size(); }

        private:
            std::span<const std::uint8_t> m_bytes;
            std::size_t m_position = 0;
        };

        // Runs of (length 1..255, value).
        template <typename T>
        void EncodeRuns(const std::vector<T> &values, std::vector<std::uint8_t> &out)
        {
            std::size_t index = 0;
            while (index < values.size())
            {
                std::size_t run = 1;
                while (run < 255 && index + run < values.size() && values[index + run] == values[index])
                    ++run;
                out.push_back(static_cast<std::uint8_t>(run));
                out.push_back(static_cast<std::uint8_t>(values[index]));
                index += run;
            }
        }

        // Decodes exactly `count` values from the start of `bytes`; returns the bytes used, or 0 when malformed.
        template <typename T>
        std::size_t DecodeRuns(std::span<const std::uint8_t> bytes, std::size_t count, std::vector<T> &values)
        {
            values.clear();
            values.reserve(count);
            std::size_t position = 0;
            while (values.size() < count)
            {
                if (position + 2 > bytes.size())
                    return 0;
                const std::size_t run = bytes[position];
                const auto value = static_cast<T>(bytes[position + 1]);
                position += 2;
                if (run == 0 || values.size() + run > count)
                    return 0;
                values.insert(values.end(), run, value);
            }
            return position;
        }

        std::optional<Contents> Fail(std::string *error, const char *reason)
        {
            if (error)
                *error = reason;
            return std::nullopt;
        }
    }

    std::vector<std::uint8_t> Encode(const VolumeGrid &grid, std::span<const std::pair<VolumeChunkCoord, VolumeChunk>> chunks)
    {
        std::vector<std::uint8_t> out;
        Writer writer(out);
        writer.Bytes(Magic, sizeof(Magic));
        writer.U32(Version);
        writer.F32(grid.voxelSize);
        writer.I32(grid.chunkVoxels);
        writer.F32(grid.origin.x);
        writer.F32(grid.origin.y);
        writer.F32(grid.origin.z);
        writer.U32(static_cast<std::uint32_t>(chunks.size()));
        std::vector<std::uint8_t> payload;
        for (const auto &[coord, chunk] : chunks)
        {
            payload.clear();
            EncodeRuns(chunk.GetDensities(), payload);
            EncodeRuns(chunk.GetMaterials(), payload);
            writer.I32(coord.x);
            writer.I32(coord.y);
            writer.I32(coord.z);
            writer.U32(static_cast<std::uint32_t>(payload.size()));
            writer.Bytes(payload.data(), payload.size());
        }
        return out;
    }

    std::vector<std::uint8_t> Encode(const TerrainVolume &volume)
    {
        std::vector<std::pair<VolumeChunkCoord, VolumeChunk>> chunks;
        for (const auto coord : volume.GetStoredChunks())
            chunks.emplace_back(coord, *volume.FindChunk(coord));
        return Encode(volume.GetGrid(), chunks);
    }

    std::optional<Contents> Decode(std::span<const std::uint8_t> bytes, std::string *error)
    {
        Reader reader(bytes);
        const auto magic = reader.Take(sizeof(Magic));
        if (magic.size() != sizeof(Magic) || std::memcmp(magic.data(), Magic, sizeof(Magic)) != 0)
            return Fail(error, "Not a volume file.");
        std::uint32_t version = 0;
        if (!reader.U32(version) || version == 0)
            return Fail(error, "The volume file's header is damaged.");
        if (version > Version)
            return Fail(error, "The volume file is from a newer engine.");

        Contents contents;
        std::uint32_t count = 0;
        if (!reader.F32(contents.grid.voxelSize) || !reader.I32(contents.grid.chunkVoxels) || !reader.F32(contents.grid.origin.x) ||
            !reader.F32(contents.grid.origin.y) || !reader.F32(contents.grid.origin.z) || !reader.U32(count))
            return Fail(error, "The volume file's header is damaged.");
        if (!contents.grid.Valid() || contents.grid.chunkVoxels > MaxChunkVoxels)
            return Fail(error, "The volume file's grid is not valid.");
        // Each chunk needs at least its header and two runs, so a count the file cannot hold is refused up front.
        if (!reader.Has(static_cast<std::size_t>(count) * (ChunkHeaderSize + 4)))
            return Fail(error, "The volume file is shorter than its chunk count.");

        const auto samples = static_cast<std::size_t>(contents.grid.chunkVoxels) * contents.grid.chunkVoxels * contents.grid.chunkVoxels;
        contents.chunks.reserve(count);
        std::vector<std::int8_t> density;
        std::vector<VolumeMaterial> material;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            VolumeChunkCoord coord;
            std::uint32_t size = 0;
            if (!reader.I32(coord.x) || !reader.I32(coord.y) || !reader.I32(coord.z) || !reader.U32(size) || !reader.Has(size))
                return Fail(error, "A chunk in the volume file is damaged.");
            const auto payload = reader.Take(size);
            const auto densityBytes = DecodeRuns(payload, samples, density);
            const auto materialBytes = densityBytes == 0 ? 0 : DecodeRuns(payload.subspan(densityBytes), samples, material);
            VolumeChunk chunk;
            if (materialBytes == 0 || densityBytes + materialBytes != payload.size() ||
                !chunk.Assign(contents.grid.chunkVoxels, std::move(density), std::move(material)))
                return Fail(error, "A chunk in the volume file is damaged.");
            contents.chunks.emplace_back(coord, std::move(chunk));
        }
        if (!reader.AtEnd())
            return Fail(error, "The volume file has data past its last chunk.");
        return contents;
    }

    bool Load(std::span<const std::uint8_t> bytes, TerrainVolume &volume, std::string *error)
    {
        auto contents = Decode(bytes, error);
        if (!contents)
            return false;
        if (!(contents->grid == volume.GetGrid()))
        {
            if (error)
                *error = "The volume file was written for another grid.";
            return false;
        }
        for (auto &[coord, chunk] : contents->chunks)
            volume.SetChunk(coord, std::move(chunk));
        return true;
    }
}
