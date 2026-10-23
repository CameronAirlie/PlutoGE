#pragma once

#include "PlutoGE/render/TerrainLayerLimits.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    class Texture;

    // Terrain material layers as the renderer sees them: up to kMaxTerrainLayers surfaces, each with its own maps
    // and world-space tiling, blended per texel by two RGBA8 weight maps (layer i is channel i % 4 of weight map
    // i / 4). A material carries one set (MaterialConfig::terrainLayers); the terrain shader variant samples the
    // layers' maps from texture arrays the RHI scene renderer builds with BuildTerrainLayerArrays.
    struct TerrainLayerSurface
    {
        const Texture *albedoMap = nullptr;    // sRGB colour; white when absent
        const Texture *normalMap = nullptr;    // OpenGL (+Y up) tangent normal; flat when absent
        const Texture *roughnessMap = nullptr; // red channel; 1 when absent
        const Texture *heightMap = nullptr;    // red channel, for height blending; 0.5 when absent
        glm::vec3 tint{1.0f};
        float tileSize = 4.0f;              // world units covered by one repeat of the maps
        float normalStrength = 1.0f;
        float roughness = 1.0f;             // multiplies the roughness map
        float metallic = 0.0f;
        bool antiTiling = false;            // stochastic tiling: hides the maps' repeats (two samples per map)
    };

    struct TerrainLayerSet
    {
        std::vector<TerrainLayerSurface> layers;
        std::array<const Texture *, kTerrainWeightMaps> weightMaps{};
        // 0 blends layers linearly by weight; larger values let the higher layer (by its height map) show through
        // the blend, e.g. stones poking out of sand, over this fraction of the height range.
        float heightBlend = 0.0f;
        // Beyond this distance from the camera (world units) the terrain shades from its macro albedo instead of
        // its layers, fading over the last quarter of the distance; 0 shows the layers at every distance.
        float detailDistance = 0.0f;
    };

    // The layers' maps resampled to one size and packed for the GPU, one array layer per terrain layer, two maps
    // per layer so shading a layer costs two texture fetches: albedo (sRGB colour, height in the linear alpha) and
    // normal (linear xyz, roughness in alpha). Only the textures shape the arrays; the per-layer scalars (tint,
    // tiling, strengths) are shader parameters, so editing them never rebuilds an array.
    struct TerrainLayerArrayPixels
    {
        std::uint32_t size = 0;
        std::uint32_t layers = 0;
        std::vector<std::byte> albedo;
        std::vector<std::byte> normal;
    };

    using TerrainTexturePixels = std::function<std::vector<std::byte>(const Texture &)>;

    // The array resolution: the largest map's power-of-two size, at most `maxSize` (at least 4).
    std::uint32_t TerrainLayerArraySize(const TerrainLayerSet &set, std::uint32_t maxSize = 2048);
    // Builds the arrays' base levels; `pixels` returns a texture's RGBA8 pixels (empty when unavailable, which
    // falls back to the map's default).
    TerrainLayerArrayPixels BuildTerrainLayerArrays(const TerrainLayerSet &set, const TerrainTexturePixels &pixels,
                                                    std::uint32_t maxSize = 2048);
}
