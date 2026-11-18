#include "PlutoGE/scene/volume/TerrainVolume.h"

#include <algorithm>

namespace PlutoGE::scene::volume
{
    namespace
    {
        // The same sample as storing would hold it.
        VolumeSample Quantized(const VolumeSample &sample, float voxelSize)
        {
            return {DequantizeDistance(QuantizeDistance(sample.distance, voxelSize), voxelSize), sample.material};
        }

        VolumeSampleBox Union(const VolumeSampleBox &a, const VolumeSampleBox &b)
        {
            if (a.Empty())
                return b;
            if (b.Empty())
                return a;
            const glm::ivec3 low = glm::min(a.first, b.first);
            const glm::ivec3 high = glm::max(a.first + a.count, b.first + b.count);
            return {low, high - low};
        }
    }

    TerrainVolume::TerrainVolume(VolumeGrid grid, std::shared_ptr<const VolumeDensitySource> base)
        : m_grid(grid), m_base(base ? std::move(base) : ConstantDensity::Air())
    {
    }

    void TerrainVolume::SetBase(std::shared_ptr<const VolumeDensitySource> base)
    {
        m_base = base ? std::move(base) : ConstantDensity::Air();
        for (const auto &[coord, chunk] : m_chunks)
            MarkDirty(m_grid.ChunkSamples(coord));
        ++m_revision;
    }

    VolumeSample TerrainVolume::Sample(glm::ivec3 sample) const
    {
        const auto coord = m_grid.ChunkOfSample(sample);
        if (const auto *chunk = FindChunk(coord))
        {
            const auto local = sample - m_grid.ChunkFirstSample(coord);
            return {DequantizeDistance(chunk->GetDensity(local), m_grid.voxelSize), chunk->GetMaterial(local)};
        }
        return Quantized(m_base->Sample(m_grid.SamplePosition(sample)), m_grid.voxelSize);
    }

    DensityBlock TerrainVolume::Gather(const VolumeSampleBox &box) const
    {
        DensityBlock block;
        block.box = box;
        block.samples.resize(box.SampleCount());
        if (box.Empty())
            return block;
        std::vector<VolumeSample> baseSamples;
        m_grid.ForEachChunk(box, [&](VolumeChunkCoord coord) {
            const auto part = box.Intersect(m_grid.ChunkSamples(coord));
            const auto chunkFirst = m_grid.ChunkFirstSample(coord);
            const auto *chunk = FindChunk(coord);
            if (!chunk)
            {
                baseSamples.resize(part.SampleCount());
                m_base->Fill(m_grid, part, baseSamples);
            }
            std::size_t partIndex = 0;
            for (int z = 0; z < part.count.z; ++z)
                for (int y = 0; y < part.count.y; ++y)
                    for (int x = 0; x < part.count.x; ++x, ++partIndex)
                    {
                        const glm::ivec3 sample = part.first + glm::ivec3(x, y, z);
                        auto &out = block.samples[block.Index(sample - box.first)];
                        if (chunk)
                        {
                            const auto local = sample - chunkFirst;
                            out = {DequantizeDistance(chunk->GetDensity(local), m_grid.voxelSize), chunk->GetMaterial(local)};
                        }
                        else
                            out = Quantized(baseSamples[partIndex], m_grid.voxelSize);
                    }
        });
        return block;
    }

    VolumeChunk TerrainVolume::MakeBaseChunk(VolumeChunkCoord coord) const
    {
        const auto box = m_grid.ChunkSamples(coord);
        std::vector<VolumeSample> samples(box.SampleCount());
        m_base->Fill(m_grid, box, samples);
        std::vector<std::int8_t> density(samples.size());
        std::vector<VolumeMaterial> material(samples.size());
        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            density[index] = QuantizeDistance(samples[index].distance, m_grid.voxelSize);
            material[index] = samples[index].material;
        }
        VolumeChunk chunk;
        chunk.Assign(m_grid.chunkVoxels, std::move(density), std::move(material));
        return chunk;
    }

    VolumeEditResult TerrainVolume::Apply(const VolumeShape &shape, const VolumeEdit &edit)
    {
        VolumeEditResult result;
        if (!m_grid.Valid())
            return result;
        // Stored distances stop at kDensityRangeVoxels: past that far outside the shape, an edit changes nothing.
        const float reach = kDensityRangeVoxels * m_grid.voxelSize;
        const auto box = m_grid.SamplesIn(shape.BoundsMin() - reach, shape.BoundsMax() + reach);
        const float voxelSize = m_grid.voxelSize;

        m_grid.ForEachChunk(box, [&](VolumeChunkCoord coord) {
            // An unstored chunk is edited as a copy of the base, and stored only if the edit changed it.
            const auto found = m_chunks.find(coord);
            VolumeChunk created;
            if (found == m_chunks.end())
                created = MakeBaseChunk(coord);
            VolumeChunk &chunk = found != m_chunks.end() ? found->second : created;
            const auto part = box.Intersect(m_grid.ChunkSamples(coord));
            const auto chunkFirst = m_grid.ChunkFirstSample(coord);
            VolumeSampleBox changed;
            for (int z = 0; z < part.count.z; ++z)
                for (int y = 0; y < part.count.y; ++y)
                    for (int x = 0; x < part.count.x; ++x)
                    {
                        const glm::ivec3 sample = part.first + glm::ivec3(x, y, z);
                        const glm::ivec3 local = sample - chunkFirst;
                        const std::int8_t density = chunk.GetDensity(local);
                        const VolumeMaterial material = chunk.GetMaterial(local);
                        const float distance = DequantizeDistance(density, voxelSize);
                        const float shapeDistance = shape.Distance(m_grid.SamplePosition(sample));
                        std::int8_t newDensity = density;
                        VolumeMaterial newMaterial = material;
                        switch (edit.mode)
                        {
                        case VolumeEditMode::Subtract:
                            newDensity = QuantizeDistance(std::max(distance, -shapeDistance), voxelSize);
                            break;
                        case VolumeEditMode::Add:
                            if (shapeDistance < distance)
                            {
                                newDensity = QuantizeDistance(shapeDistance, voxelSize);
                                newMaterial = edit.material;
                            }
                            break;
                        case VolumeEditMode::Paint:
                            // Rock inside the shape, and the air just above it (so the surface's vertices see it).
                            if (shapeDistance < 0.0f && distance < voxelSize)
                                newMaterial = edit.material;
                            break;
                        }
                        if (newDensity == density && newMaterial == material)
                            continue;
                        chunk.Set(local, newDensity, newMaterial);
                        changed = Union(changed, {sample, glm::ivec3(1)});
                    }
            if (changed.Empty())
                return;
            if (found == m_chunks.end())
                m_chunks.emplace(coord, std::move(created));
            result.changedChunks.push_back(coord);
            result.changedSamples = Union(result.changedSamples, changed);
        });

        if (result.Changed())
        {
            MarkDirty(result.changedSamples);
            ++m_revision;
        }
        return result;
    }

    const VolumeChunk *TerrainVolume::FindChunk(VolumeChunkCoord chunk) const
    {
        const auto found = m_chunks.find(chunk);
        return found == m_chunks.end() ? nullptr : &found->second;
    }

    std::vector<VolumeChunkCoord> TerrainVolume::GetStoredChunks() const
    {
        std::vector<VolumeChunkCoord> chunks;
        chunks.reserve(m_chunks.size());
        for (const auto &[coord, chunk] : m_chunks)
            chunks.push_back(coord);
        std::sort(chunks.begin(), chunks.end());
        return chunks;
    }

    bool TerrainVolume::SetChunk(VolumeChunkCoord coord, VolumeChunk samples)
    {
        if (!samples.Valid() || samples.GetSamplesPerSide() != m_grid.chunkVoxels)
            return false;
        m_chunks.insert_or_assign(coord, std::move(samples));
        MarkDirty(m_grid.ChunkSamples(coord));
        ++m_revision;
        return true;
    }

    bool TerrainVolume::RemoveChunk(VolumeChunkCoord coord)
    {
        if (m_chunks.erase(coord) == 0)
            return false;
        MarkDirty(m_grid.ChunkSamples(coord));
        ++m_revision;
        return true;
    }

    std::vector<VolumeChunkCoord> TerrainVolume::TakeDirtyChunks()
    {
        std::vector<VolumeChunkCoord> dirty(m_dirty.begin(), m_dirty.end());
        m_dirty.clear();
        return dirty;
    }

    void TerrainVolume::MarkDirty(const VolumeSampleBox &changedSamples)
    {
        // A chunk's mesh reads its samples and an apron around them, so a change reaches the meshes of every chunk
        // within the apron of it.
        const VolumeSampleBox reach{changedSamples.first - m_meshApron, changedSamples.count + 2 * m_meshApron};
        m_grid.ForEachChunk(reach, [this](VolumeChunkCoord coord) { m_dirty.insert(coord); });
    }
}
