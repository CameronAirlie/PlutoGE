#pragma once

#include "PlutoGE/scene/TerrainEditSurface.h"
#include "PlutoGE/scene/TerrainTileSource.h"
#include "PlutoGE/scene/TerrainWorldGrid.h"
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/procedural/ProceduralGeneration.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
    class TerrainComponent;

    // A terrain world too large for one terrain (docs/TERRAIN_WORLDS.md): a grid of tiles (TerrainWorldGrid), each a
    // .plutoheight file, streamed in and out around a viewer as transient child entities, with a low-detail overview
    // drawn wherever a detailed tile is not loaded.
    //
    // A tile is built from the tile prefab when there is one (any components: the terrain's material and layers, a
    // collider, detail grass, a ProceduralTerrainComponent that paints and plants it) or else from a terrain with the
    // tile material and a terrain collider; then it takes its height file. Reading and decoding run on worker threads,
    // and so does procedural generation; a few tiles a frame become terrains. The tiles keep their height file, so
    // sculpting a tile in the editor saves into it.
    class TerrainWorldComponent : public TypedComponent<TerrainWorldComponent>
    {
    public:
        TerrainWorldComponent() = default;
        ~TerrainWorldComponent() override;

        void Update(float deltaTime) override;
        void OnRemoved() override;
        void OnEnabledChanged() override;
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const TerrainWorldGrid &GetGrid() const { return m_grid; }
        // Changing the grid unloads every tile.
        void SetGrid(const TerrainWorldGrid &grid);
        const TerrainStreamingSettings &GetStreaming() const { return m_streaming; }
        void SetStreaming(const TerrainStreamingSettings &settings) { m_streaming = settings; }
        // Tile files: an asset reference pattern with {x} and {z}.
        const std::string &GetTileFiles() const { return m_tileFiles; }
        void SetTileFiles(std::string pattern);
        const std::string &GetTilePrefab() const { return m_tilePrefab; }
        void SetTilePrefab(std::string prefab) { m_tilePrefab = std::move(prefab); }
        const std::string &GetTileMaterial() const { return m_tileMaterial; }
        void SetTileMaterial(std::string material) { m_tileMaterial = std::move(material); }
        float GetTileSkirtDepth() const { return m_tileSkirtDepth; }
        void SetTileSkirtDepth(float depth) { m_tileSkirtDepth = std::max(depth, 0.0f); }
        // The overview: a .plutoheight of the whole world at low detail, drawn where tiles are not loaded.
        const std::string &GetOverviewFile() const { return m_overviewFile; }
        void SetOverviewFile(std::string file);
        const std::string &GetOverviewMaterial() const { return m_overviewMaterial; }
        void SetOverviewMaterial(std::string material) { m_overviewMaterial = std::move(material); }
        bool GetStreamInEditor() const { return m_streamInEditor; }
        void SetStreamInEditor(bool stream) { m_streamInEditor = stream; }

        // Where the world streams around: an override (gameplay, tests), else the editor's camera while the game is not
        // running, else the scene's main camera.
        void SetViewerOverride(std::optional<glm::vec3> worldPosition) { m_viewerOverride = worldPosition; }
        static void SetEditorViewer(std::optional<glm::vec3> worldPosition);
        [[nodiscard]] std::optional<glm::vec3> GetViewer() const;

        [[nodiscard]] TerrainTileState GetTileState(TerrainTileCoord tile) const;
        [[nodiscard]] Entity *GetTileEntity(TerrainTileCoord tile) const;
        [[nodiscard]] TerrainComponent *GetTileTerrain(TerrainTileCoord tile) const;
        [[nodiscard]] Entity *GetOverviewEntity(TerrainTileCoord tile) const;
        [[nodiscard]] std::size_t GetActiveTileCount() const;
        // Problems met while streaming (missing files, decoding errors), newest last; kept short.
        [[nodiscard]] const std::vector<std::string> &GetStreamingErrors() const { return m_errors; }

        // Streams around a position until every tile within the load radius is active and generated, or the timeout
        // passes (loading screens, tests, tools). True when everything arrived.
        bool LoadAround(glm::vec3 worldPosition, std::chrono::milliseconds timeout = std::chrono::seconds(30));
        void UnloadAll();
        // Drops the tiles and the overview so both are read again (after the files were rewritten).
        void Reload();

        // The ground at a world position: the detailed tile when loaded, else the overview. Nothing off the world.
        [[nodiscard]] std::optional<float> SampleHeight(glm::vec3 worldPosition) const;

        // ---- where tiles come from -------------------------------------------------------------------------------
        // The Tile Template: an inactive child entity (tagged TileTemplateTag) whose components every tile copies:
        // terrain settings and material layers, collider, foliage types, detail grass, biomes. Edited like any entity;
        // the world notices changes and refreshes the loaded tiles. A tile prefab, when set, is used instead.
        static constexpr std::string_view TileTemplateTag = "TerrainTileTemplate";
        [[nodiscard]] Entity *GetTileTemplate() const;
        // The template, created (a terrain with the tile material and a terrain collider) when there is none.
        Entity *EnsureTileTemplate();
        [[nodiscard]] static bool IsTileTemplate(const Entity &entity);
        // The source tiles are made from now: the prefab, else the template, else a plain terrain.
        [[nodiscard]] std::unique_ptr<TerrainTileSource> MakeTileSource() const;
        // Applies the source's settings to the loaded tiles (heights, holes and painted weights stay) and regenerates
        // their biomes; rebuilds them when that cannot be done in place.
        void RefreshTiles();

        // ---- editing ---------------------------------------------------------------------------------------------
        // The loaded tiles' terrains, for brushes.
        [[nodiscard]] TerrainEditSurface GetEditSurface() const;
        // The tile an entity is (a loaded tile entity), if any.
        [[nodiscard]] std::optional<TerrainTileCoord> FindTile(const Entity &entity) const;
        [[nodiscard]] std::optional<TerrainTileCoord> FindTile(const TerrainComponent &terrain) const;
        // After the touched tiles' heights changed: makes the samples they share with their neighbours agree and
        // refreshes everyone's border rings. Loaded neighbours change in memory, unloaded ones in their files.
        // Returns the loaded terrains whose heights changed (whose height files the caller saves).
        std::vector<TerrainComponent *> SyncTileEdges(const std::vector<TerrainTileCoord> &touched);
        // Writes a region of heights into a tile: the loaded terrain (returned, for the caller to save), or the tile's
        // file when it is not loaded (null). False when neither is possible.
        bool PatchTileHeights(TerrainTileCoord tile, const TerrainHeightRegion &region, TerrainComponent **loaded = nullptr);
        // A tile's layer weight file (beside its height file): painted tiles keep their weights, and biomes then only
        // plant them.
        [[nodiscard]] std::string TileWeightFile(TerrainTileCoord tile, std::size_t plane) const;

        // Writes a world's files: one .plutoheight per tile (with border rings) and the overview. `heights` is the whole
        // world in world units (WorldSamplesX x WorldSamplesZ; other sizes are resampled). Paths are file paths, the
        // tile one a pattern with {x} and {z}.
        struct WriteResult
        {
            int tilesWritten = 0;
            std::string error;
            [[nodiscard]] bool Ok() const { return error.empty(); }
        };
        static WriteResult WriteWorldFiles(const TerrainHeightField &heights, const TerrainWorldGrid &grid, const std::string &tilePathPattern,
                                           const std::string &overviewPath, int overviewSamplesPerTile = 16,
                                           TerrainHeightEncoding encoding = TerrainHeightEncoding::Float32);

    private:
        struct Tile
        {
            TerrainTileState state = TerrainTileState::Unloaded;
            std::future<std::optional<TerrainHeightData>> load;
            std::optional<TerrainHeightData> data;
            std::uint32_t entity = 0;
            std::future<procedural::ProceduralGenerationResult> generation;
            bool generating = false;
            bool paintedWeights = false; // the tile's weights come from its own files, not its biomes
            std::uint64_t sourceSignature = 0; // the tile source's signature when the tile was made or refreshed
        };

        void Stream(bool unbudgeted);
        void StartLoad(TerrainTileCoord tile, Tile &state);
        void Activate(TerrainTileCoord tile, Tile &state);
        void Unload(TerrainTileCoord tile, Tile &state);
        void PollGeneration(Tile &state, bool wait);
        void EnsureOverview();
        void ClearOverview();
        void ValidateEntities();
        void InvalidateNavigation(TerrainTileCoord tile) const;
        void ReportError(std::string message);
        void Abandon(Tile &state);
        void StartGeneration(Tile &state, Entity &tile);
        void WatchTileSource(float deltaTime);

        TerrainWorldGrid m_grid;
        TerrainStreamingSettings m_streaming;
        std::string m_tileFiles = "project://TerrainWorld/tile_{x}_{z}.plutoheight";
        std::string m_tilePrefab;
        std::string m_tileMaterial;
        float m_tileSkirtDepth = 2.0f;
        std::string m_overviewFile;
        std::string m_overviewMaterial;
        float m_overviewSkirtDepth = 20.0f;
        bool m_streamInEditor = true;
        std::optional<glm::vec3> m_viewerOverride;

        std::map<TerrainTileCoord, Tile> m_tiles;
        // Work whose result is no longer wanted; kept until it finishes (a future from std::async blocks when dropped).
        std::vector<std::future<std::optional<TerrainHeightData>>> m_abandonedLoads;
        std::vector<std::future<procedural::ProceduralGenerationResult>> m_abandonedGenerations;

        std::optional<TerrainHeightField> m_overview;
        bool m_overviewTried = false;
        std::map<TerrainTileCoord, std::uint32_t> m_overviewEntities;
        std::vector<std::string> m_errors;
        // When the tile source is next checked for changes.
        float m_sourceCheckTimer = 0.0f;
    };
}
