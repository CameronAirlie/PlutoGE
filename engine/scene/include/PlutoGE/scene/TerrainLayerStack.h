#pragma once

#include "PlutoGE/render/TerrainLayers.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/TerrainWeightMap.h"

#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    // One terrain material layer as authored: its maps (texture asset references; any may be empty) and how they
    // tile and shade. Maps follow the material conventions: sRGB albedo, OpenGL (+Y up) normals, roughness and
    // height in the red channel.
    struct TerrainLayer
    {
        std::string name;
        std::string albedoMap, normalMap, roughnessMap, heightMap;
        glm::vec3 tint{1.0f};
        float tileSize = 4.0f;       // world units per repeat of the maps
        float normalStrength = 1.0f;
        float roughness = 1.0f;      // multiplies the roughness map
        float metallic = 0.0f;
        bool antiTiling = false;     // hides the maps' repeats at the cost of a second sample per map

        bool operator==(const TerrainLayer &) const = default;
    };

    // A terrain's material layers and their painted weights, and what the renderer needs of them: a
    // render::TerrainLayerSet, the weight maps as textures, and a low-detail "macro" albedo (each layer's mean
    // colour by weight) for GI, baking and renderers without the terrain shader.
    //
    // Engine services (texture loading, path resolution, file reads) are injected, so the stack is usable and
    // testable without an engine. Weight maps persist as one or two PNGs (layers 0-3, 4-7 in RGBA), stored like
    // height maps: the image's top row is the terrain's far (+z) edge.
    class TerrainLayerStack
    {
    public:
        static constexpr std::size_t kMaxLayers = render::kMaxTerrainLayers;
        static constexpr int kMacroResolution = 256;

        struct Services
        {
            std::function<const render::Texture *(const std::string &reference, render::TextureColorSpace)> loadTexture;
            // An asset reference or path to a file path (empty when it cannot be resolved).
            std::function<std::string(const std::string &reference)> resolvePath;
            // A file's bytes (content packs as well as loose files); false when missing.
            std::function<bool(const std::string &path, std::string &bytes)> readFile;
        };
        // A restorable copy of the layers and weights, for undoing edits that change both.
        struct Snapshot
        {
            std::vector<TerrainLayer> layers;
            TerrainWeightMap weights;
        };

        explicit TerrainLayerStack(Services services = {});

        // ---- layers --------------------------------------------------------------------------------------------
        [[nodiscard]] const std::vector<TerrainLayer> &GetLayers() const { return m_layers; }
        [[nodiscard]] bool Empty() const { return m_layers.empty(); }
        // Appends a layer (false when full). The first layer covers the whole terrain; later ones start unpainted.
        bool AddLayer(TerrainLayer layer);
        void SetLayer(std::size_t index, TerrainLayer layer);
        // Removes a layer; its weight goes to the others in proportion.
        void RemoveLayer(std::size_t index);
        void MoveLayer(std::size_t from, std::size_t to);
        // Replaces the layers without touching the weights (deserialization).
        void SetLayers(std::vector<TerrainLayer> layers);

        [[nodiscard]] float GetHeightBlend() const { return m_heightBlend; }
        void SetHeightBlend(float heightBlend);
        // Distance from the camera beyond which the terrain shows its macro albedo instead of the layers (a cheap
        // stand-in for far terrain); 0 shows the layers everywhere.
        [[nodiscard]] float GetDetailDistance() const { return m_detailDistance; }
        void SetDetailDistance(float distance);

        // ---- weights -------------------------------------------------------------------------------------------
        [[nodiscard]] const TerrainWeightMap &GetWeights() const { return m_weights; }
        TerrainWeightRect Paint(glm::vec2 uv, glm::vec2 radiusUv, std::size_t layer, const TerrainLayerBrush &brush);
        void Fill(std::size_t layer);
        void SetWeightResolution(int resolution);
        void SetWeights(TerrainWeightMap weights);
        void RestoreWeights(const TerrainWeightMap::Region &region);

        [[nodiscard]] Snapshot Capture() const { return {m_layers, m_weights}; }
        void Restore(const Snapshot &snapshot);

        // ---- weight map files ----------------------------------------------------------------------------------
        // Asset references (or paths) of the weight PNGs; empty until saved. The second is only used past 4 layers.
        [[nodiscard]] const std::array<std::string, render::kTerrainWeightMaps> &GetWeightMapPaths() const { return m_weightMapPaths; }
        void SetWeightMapPaths(std::array<std::string, render::kTerrainWeightMaps> paths) { m_weightMapPaths = std::move(paths); }
        // Loads the weights from the stored paths (missing maps leave those layers unpainted).
        bool LoadWeightMaps();
        // Writes the weights to the stored paths' files (resolved through the services).
        bool SaveWeightMaps() const;
        // PNG encoding and decoding of the weight planes, in the file orientation (top row = far edge).
        static std::vector<std::uint8_t> EncodeWeightPlane(const TerrainWeightMap &weights, std::size_t plane);
        static bool DecodeWeightPlanes(std::span<const std::string> files, TerrainWeightMap &weights);

        // ---- render data ---------------------------------------------------------------------------------------
        // Null without layers. A new set is made when layers change (not when weights do: the weight textures
        // change in place).
        [[nodiscard]] std::shared_ptr<const render::TerrainLayerSet> GetRenderSet();
        [[nodiscard]] const render::Texture *GetMacroAlbedo();

    private:
        void MarkLayersChanged();
        // The whole weight map changed, or just a rectangle of it (painting, undo): textures are then patched there.
        void MarkWeightsChanged();
        void MarkWeightsChanged(const TerrainWeightRect &rect);
        void UpdateWeightTextures();
        void UpdateMacroAlbedo();
        // The macro albedo's texels in `macroRect`, packed.
        std::vector<unsigned char> MacroPixels(const render::TextureRect &macroRect);
        glm::vec3 MeanLinearColour(std::size_t layer);

        Services m_services;
        std::vector<TerrainLayer> m_layers;
        TerrainWeightMap m_weights;
        float m_heightBlend = 0.0f;
        float m_detailDistance = 0.0f;
        std::array<std::string, render::kTerrainWeightMaps> m_weightMapPaths;

        std::shared_ptr<const render::TerrainLayerSet> m_renderSet;
        std::array<std::unique_ptr<render::Texture>, render::kTerrainWeightMaps> m_weightTextures;
        std::unique_ptr<render::Texture> m_macroAlbedo;
        std::vector<glm::vec3> m_meanColours;   // per layer, linear; empty when stale
        // Weight changes not yet in the weight textures and the macro albedo: everything, or a rectangle.
        bool m_weightTexturesStale = true;
        TerrainWeightRect m_weightTexturesDirty;
        bool m_macroStale = true;
        TerrainWeightRect m_macroDirty;
    };
}
