#include "PlutoGE/scene/volume/VolumeGrid.h"

namespace PlutoGE::scene::volume
{
    VolumeSampleBox VolumeSampleBox::Intersect(const VolumeSampleBox &other) const
    {
        const glm::ivec3 low = glm::max(first, other.first);
        const glm::ivec3 high = glm::min(first + count, other.first + other.count);
        if (glm::any(glm::lessThanEqual(high, low)))
            return {};
        return {low, high - low};
    }

    VolumeSampleBox VolumeGrid::SamplesIn(const glm::vec3 &minimum, const glm::vec3 &maximum) const
    {
        const glm::ivec3 low(glm::floor((minimum - origin) / voxelSize));
        const glm::ivec3 high(glm::ceil((maximum - origin) / voxelSize));
        if (glm::any(glm::lessThan(high, low)))
            return {};
        return {low, high - low + 1};
    }

    void VolumeGrid::ForEachChunk(const VolumeSampleBox &box, const std::function<void(VolumeChunkCoord)> &visit) const
    {
        if (box.Empty())
            return;
        const auto low = ChunkOfSample(box.first);
        const auto high = ChunkOfSample(box.Last());
        for (int z = low.z; z <= high.z; ++z)
            for (int y = low.y; y <= high.y; ++y)
                for (int x = low.x; x <= high.x; ++x)
                    visit({x, y, z});
    }
}
