#include "PlutoGE/scene/volume/VolumeChunk.h"

namespace PlutoGE::scene::volume
{
    VolumeChunk::VolumeChunk(int samplesPerSide, std::int8_t fill, VolumeMaterial material)
        : m_side(samplesPerSide > 0 ? samplesPerSide : 0)
    {
        const auto count = static_cast<std::size_t>(m_side) * m_side * m_side;
        m_density.assign(count, fill);
        m_material.assign(count, material);
    }

    bool VolumeChunk::Assign(int samplesPerSide, std::vector<std::int8_t> density, std::vector<VolumeMaterial> material)
    {
        if (samplesPerSide <= 0)
            return false;
        const auto count = static_cast<std::size_t>(samplesPerSide) * samplesPerSide * samplesPerSide;
        if (density.size() != count || material.size() != count)
            return false;
        m_side = samplesPerSide;
        m_density = std::move(density);
        m_material = std::move(material);
        return true;
    }

    bool VolumeChunk::HasRock() const
    {
        return std::any_of(m_density.begin(), m_density.end(), [](std::int8_t value) { return value < 0; });
    }

    bool VolumeChunk::HasAir() const
    {
        return std::any_of(m_density.begin(), m_density.end(), [](std::int8_t value) { return value >= 0; });
    }
}
