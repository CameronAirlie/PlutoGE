#include "PlutoGE/scene/volume/VolumeDensitySource.h"

#include <algorithm>

namespace PlutoGE::scene::volume
{
    namespace
    {
        // Calls visit(sampleIndex, position) for every sample of a box, x fastest, then y, then z.
        template <typename Visit>
        void ForEachSample(const VolumeGrid &grid, const VolumeSampleBox &box, Visit &&visit)
        {
            std::size_t index = 0;
            for (int z = 0; z < box.count.z; ++z)
                for (int y = 0; y < box.count.y; ++y)
                    for (int x = 0; x < box.count.x; ++x)
                        visit(index++, grid.SamplePosition(box.first + glm::ivec3(x, y, z)));
        }
    }

    void VolumeDensitySource::Fill(const VolumeGrid &grid, const VolumeSampleBox &box, std::span<VolumeSample> out) const
    {
        if (out.size() < box.SampleCount())
            return;
        ForEachSample(grid, box, [&](std::size_t index, const glm::vec3 &position) { out[index] = Sample(position); });
    }

    VolumeSample HeightfieldDensity::Sample(const glm::vec3 &position) const
    {
        const auto height = m_height ? m_height(position.x, position.z) : std::nullopt;
        if (!height)
            return {1.0e6f, m_material};
        return {position.y - *height, m_material};
    }

    void HeightfieldDensity::Fill(const VolumeGrid &grid, const VolumeSampleBox &box, std::span<VolumeSample> out) const
    {
        if (out.size() < box.SampleCount())
            return;
        const auto columns = static_cast<std::size_t>(box.count.x);
        const auto rows = static_cast<std::size_t>(box.count.y);
        for (int z = 0; z < box.count.z; ++z)
            for (int x = 0; x < box.count.x; ++x)
            {
                const auto base = grid.SamplePosition(box.first + glm::ivec3(x, 0, z));
                const auto height = m_height ? m_height(base.x, base.z) : std::nullopt;
                for (int y = 0; y < box.count.y; ++y)
                {
                    const float sampleY = base.y + static_cast<float>(y) * grid.voxelSize;
                    out[(static_cast<std::size_t>(z) * rows + static_cast<std::size_t>(y)) * columns + static_cast<std::size_t>(x)] = {
                        height ? sampleY - *height : 1.0e6f, m_material};
                }
            }
    }

    VolumeSample CompositeDensity::Combine(const VolumeSample &base, const VolumeSample &layer, Operation operation)
    {
        switch (operation)
        {
        case Operation::Union:
            return layer.distance < base.distance ? layer : base;
        case Operation::Subtract:
            return {std::max(base.distance, -layer.distance), base.material};
        case Operation::Intersect:
            return {std::max(base.distance, layer.distance), base.material};
        case Operation::Paint:
            return layer.distance < 0.0f ? VolumeSample{base.distance, layer.material} : base;
        }
        return base;
    }

    VolumeSample CompositeDensity::Sample(const glm::vec3 &position) const
    {
        if (m_layers.empty())
            return {1.0e6f, 0};
        VolumeSample result = m_layers.front().source->Sample(position);
        for (std::size_t index = 1; index < m_layers.size(); ++index)
            result = Combine(result, m_layers[index].source->Sample(position), m_layers[index].operation);
        return result;
    }

    void CompositeDensity::AppendFeatureBounds(std::vector<VolumeBounds> &out) const
    {
        for (const auto &layer : m_layers)
            layer.source->AppendFeatureBounds(out);
    }

    void CompositeDensity::Fill(const VolumeGrid &grid, const VolumeSampleBox &box, std::span<VolumeSample> out) const
    {
        const auto count = box.SampleCount();
        if (out.size() < count)
            return;
        if (m_layers.empty())
        {
            std::fill_n(out.begin(), count, VolumeSample{1.0e6f, 0});
            return;
        }
        // Each layer fills the whole box (so its own bulk path runs), then combines into the result.
        m_layers.front().source->Fill(grid, box, out);
        std::vector<VolumeSample> layerSamples(count);
        for (std::size_t index = 1; index < m_layers.size(); ++index)
        {
            m_layers[index].source->Fill(grid, box, layerSamples);
            for (std::size_t sample = 0; sample < count; ++sample)
                out[sample] = Combine(out[sample], layerSamples[sample], m_layers[index].operation);
        }
    }
}
