#pragma once

#include "PlutoGE/render/TerrainLayerLimits.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace PlutoGE::scene
{
    // How a terrain layer brush lays paint down. Radii are in the weight map's 0..1 UVs (the caller converts from
    // terrain units, which keeps the brush round on non-square terrains).
    struct TerrainLayerBrush
    {
        float strength = 0.5f; // weight added at the brush centre by one dab, 0..1
        float hardness = 0.5f; // fraction of the radius at full strength before the falloff, 0..1
    };

    // A rectangle of weight-map texels, [x0, x1) x [y0, y1).
    struct TerrainWeightRect
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        [[nodiscard]] bool Empty() const { return x1 <= x0 || y1 <= y0; }
        void Include(const TerrainWeightRect &other);
        bool operator==(const TerrainWeightRect &) const = default;
    };

    // Per-texel weights of up to kMaxTerrainLayers terrain layers over the terrain's 0..1 UVs (x along the
    // terrain's local x, y along its local z). The weights of every texel always sum to 255. On the GPU they are
    // two RGBA8 images (Plane): layer i is channel i % 4 of plane i / 4.
    class TerrainWeightMap
    {
    public:
        static constexpr std::size_t kLayers = render::kMaxTerrainLayers;
        static constexpr std::size_t kPlanes = render::kTerrainWeightMaps;

        // Square, `resolution` texels a side, all weight on layer 0.
        explicit TerrainWeightMap(int resolution = 256);

        [[nodiscard]] int GetResolution() const { return m_resolution; }
        [[nodiscard]] std::uint8_t GetWeight(int x, int y, std::size_t layer) const;
        // Bilinear weight of `layer` at a 0..1 UV (clamped), 0..1.
        [[nodiscard]] float SampleWeight(glm::vec2 uv, std::size_t layer) const;
        // The layer with the most weight at a UV, for surface queries (footsteps, decals).
        [[nodiscard]] std::size_t DominantLayer(glm::vec2 uv) const;

        // Adds `layer` around `centre` with an elliptical brush of `radius` (UVs); returns the texels changed.
        TerrainWeightRect Paint(glm::vec2 centre, glm::vec2 radius, std::size_t layer, const TerrainLayerBrush &brush);
        // Gives every texel entirely to `layer`.
        void Fill(std::size_t layer);
        // Removes a layer: its weight goes to the remaining layers in proportion (or to layer 0 where it was alone)
        // and the layers above it move down one.
        void RemoveLayer(std::size_t layer);
        // Moves a layer's weights to another index, shifting the layers between (keeps weights with their layer
        // when the layer list is reordered).
        void MoveLayer(std::size_t from, std::size_t to);
        // Resamples to a new resolution (bilinear), keeping the painting.
        void Resize(int resolution);

        // Plane `plane` as RGBA8 rows, row 0 at UV y = 0 (the whole map, or a rectangle of it packed tightly).
        [[nodiscard]] std::vector<std::uint8_t> GetPlane(std::size_t plane) const;
        [[nodiscard]] std::vector<std::uint8_t> GetPlane(std::size_t plane, const TerrainWeightRect &rect) const;
        // Replaces the weights from planes of `resolution` (as GetPlane); renormalizes texels that do not sum to
        // 255 and gives empty texels to layer 0. False if the data does not match.
        bool SetPlanes(int resolution, std::span<const std::vector<std::uint8_t>> planes);

        // A copy of a rectangle's weights, for undo.
        struct Region
        {
            TerrainWeightRect rect;
            std::vector<std::uint8_t> weights;
        };
        [[nodiscard]] Region Capture(const TerrainWeightRect &rect) const;
        void Restore(const Region &region);

        bool operator==(const TerrainWeightMap &) const = default;

    private:
        [[nodiscard]] std::size_t Index(int x, int y) const { return (static_cast<std::size_t>(y) * m_resolution + x) * kLayers; }
        void Normalize(std::uint8_t *texel) const;

        int m_resolution = 0;
        std::vector<std::uint8_t> m_weights; // kLayers bytes per texel, rows of increasing y
    };
}
