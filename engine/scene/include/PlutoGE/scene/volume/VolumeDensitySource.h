#pragma once

#include "PlutoGE/scene/volume/VolumeChunk.h"
#include "PlutoGE/scene/volume/VolumeShape.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace PlutoGE::scene::volume
{
    // What a terrain volume is before anything edits it: the heightfield's ground, generated caves, or a mix of them.
    // Only edited chunks are stored, so a source must give the same answer every time it is asked (deterministic, and
    // immutable once shared): meshing workers read sources concurrently.
    class VolumeDensitySource
    {
    public:
        virtual ~VolumeDensitySource() = default;
        // The volume at a position in the volume's space.
        [[nodiscard]] virtual VolumeSample Sample(const glm::vec3 &position) const = 0;
        // Every sample of a box of the grid's lattice, x fastest, then y, then z (`out` holds box.SampleCount()).
        // The default asks Sample for each; sources with cheaper bulk evaluation (a column at a time) override it.
        virtual void Fill(const VolumeGrid &grid, const VolumeSampleBox &box, std::span<VolumeSample> out) const;
        // Where this source makes something other than plain ground (generated caves, a stamped shape): boxes in its
        // space. A volume meshes the chunks they touch even when nothing was edited there, and looks there for cave
        // mouths. Ground and constant sources add nothing.
        virtual void AppendFeatureBounds(std::vector<VolumeBounds> &) const {}
    };

    // The same everywhere: all air (positive distance) or all rock.
    class ConstantDensity final : public VolumeDensitySource
    {
    public:
        explicit ConstantDensity(float distance, VolumeMaterial material = 0) : m_sample{distance, material} {}
        [[nodiscard]] static std::shared_ptr<ConstantDensity> Air() { return std::make_shared<ConstantDensity>(1.0e6f); }
        [[nodiscard]] static std::shared_ptr<ConstantDensity> Rock(VolumeMaterial material = 0)
        {
            return std::make_shared<ConstantDensity>(-1.0e6f, material);
        }
        [[nodiscard]] VolumeSample Sample(const glm::vec3 &) const override { return m_sample; }

    private:
        VolumeSample m_sample;
    };

    // Ground below a height function, rock beneath it: distance = y - height(x, z), the vertical distance (exact on
    // flat ground, larger than the true distance on slopes, which meshing tolerates). Where the function has no height
    // (off the terrain) it is air. The function is called from worker threads.
    class HeightfieldDensity final : public VolumeDensitySource
    {
    public:
        using HeightFunction = std::function<std::optional<float>(float x, float z)>;
        explicit HeightfieldDensity(HeightFunction height, VolumeMaterial material = 0)
            : m_height(std::move(height)), m_material(material)
        {
        }
        [[nodiscard]] VolumeSample Sample(const glm::vec3 &position) const override;
        // A column at a time: the height function is called once per (x, z).
        void Fill(const VolumeGrid &grid, const VolumeSampleBox &box, std::span<VolumeSample> out) const override;

    private:
        HeightFunction m_height;
        VolumeMaterial m_material;
    };

    // Sources combined in order, each into the running result: the first is the base, and each later one is united
    // with it (adds rock), subtracted from it (carves air: caves) or intersected with it.
    class CompositeDensity final : public VolumeDensitySource
    {
    public:
        enum class Operation
        {
            Union,     // rock where either is rock (min of distances); the added source's material where it wins
            Subtract,  // the source's rock becomes air (max(base, -source))
            Intersect, // rock only where both are (max of distances)
            Paint,     // rock inside the source takes the source's material; the surface does not move
        };
        struct Layer
        {
            std::shared_ptr<const VolumeDensitySource> source;
            Operation operation = Operation::Union;
        };

        CompositeDensity() = default;
        explicit CompositeDensity(std::vector<Layer> layers);
        void Add(std::shared_ptr<const VolumeDensitySource> source, Operation operation);
        [[nodiscard]] const std::vector<Layer> &GetLayers() const { return m_layers; }
        // With no layers, air.
        [[nodiscard]] VolumeSample Sample(const glm::vec3 &position) const override;
        void Fill(const VolumeGrid &grid, const VolumeSampleBox &box, std::span<VolumeSample> out) const override;
        // Every layer's features.
        void AppendFeatureBounds(std::vector<VolumeBounds> &out) const override;

        // How a layer's sample combines into the running one (the CSG rule shared with edits).
        [[nodiscard]] static VolumeSample Combine(const VolumeSample &base, const VolumeSample &layer, Operation operation);

    private:
        // Where a layer can change anything: the union of its features. Union, Subtract and Paint layers leave samples
        // outside it (beyond the stored range) as they were, so Fill skips them there: a base of many stamps costs only
        // the stamps near each block.
        struct Envelope
        {
            bool bounded = false;
            VolumeBounds bounds;
        };
        [[nodiscard]] static Envelope EnvelopeOf(const Layer &layer);
        [[nodiscard]] bool Reaches(std::size_t layer, const glm::vec3 &low, const glm::vec3 &high, float reach) const;

        std::vector<Layer> m_layers;
        std::vector<Envelope> m_envelopes;
    };

    // A shape as a source: its inside is rock of one material. Placed into a composite, shapes become stamps (a
    // subtracted capsule is a tunnel) without being edits.
    class ShapeDensity final : public VolumeDensitySource
    {
    public:
        explicit ShapeDensity(std::shared_ptr<const VolumeShape> shape, VolumeMaterial material = 0)
            : m_shape(std::move(shape)), m_material(material)
        {
        }
        [[nodiscard]] VolumeSample Sample(const glm::vec3 &position) const override { return {m_shape->Distance(position), m_material}; }
        void AppendFeatureBounds(std::vector<VolumeBounds> &out) const override { out.push_back({m_shape->BoundsMin(), m_shape->BoundsMax()}); }

    private:
        std::shared_ptr<const VolumeShape> m_shape;
        VolumeMaterial m_material;
    };
}
