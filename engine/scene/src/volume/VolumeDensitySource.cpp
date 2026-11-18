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

    CompositeDensity::CompositeDensity(std::vector<Layer> layers)
    {
        for (auto &layer : layers)
            Add(std::move(layer.source), layer.operation);
    }

    void CompositeDensity::Add(std::shared_ptr<const VolumeDensitySource> source, Operation operation)
    {
        m_layers.push_back({std::move(source), operation});
        m_envelopes.push_back(EnvelopeOf(m_layers.back()));
    }

    CompositeDensity::Envelope CompositeDensity::EnvelopeOf(const Layer &layer)
    {
        Envelope envelope;
        if (!layer.source || layer.operation == Operation::Intersect)
            return envelope; // an intersection changes everything outside its source
        std::vector<VolumeBounds> features;
        layer.source->AppendFeatureBounds(features);
        if (features.empty())
            return envelope;
        envelope.bounded = true;
        envelope.bounds = features.front();
        for (const auto &feature : features)
        {
            envelope.bounds.min = glm::min(envelope.bounds.min, feature.min);
            envelope.bounds.max = glm::max(envelope.bounds.max, feature.max);
        }
        return envelope;
    }

    bool CompositeDensity::Reaches(std::size_t layer, const glm::vec3 &low, const glm::vec3 &high, float reach) const
    {
        const auto &envelope = m_envelopes[layer];
        return !envelope.bounded || (glm::all(glm::lessThanEqual(envelope.bounds.min - reach, high)) &&
                                     glm::all(glm::greaterThanEqual(envelope.bounds.max + reach, low)));
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
        // Each layer fills the whole box (so its own bulk path runs), then combines into the result; layers that cannot
        // reach the box are skipped.
        m_layers.front().source->Fill(grid, box, out);
        std::vector<VolumeSample> layerSamples(count);
        const float reach = kDensityRangeVoxels * grid.voxelSize + grid.voxelSize;
        const glm::vec3 low = grid.SamplePosition(box.first), high = grid.SamplePosition(box.Last());
        for (std::size_t index = 1; index < m_layers.size(); ++index)
        {
            if (!Reaches(index, low, high, reach))
                continue;
            m_layers[index].source->Fill(grid, box, layerSamples);
            for (std::size_t sample = 0; sample < count; ++sample)
                out[sample] = Combine(out[sample], layerSamples[sample], m_layers[index].operation);
        }
    }
}
