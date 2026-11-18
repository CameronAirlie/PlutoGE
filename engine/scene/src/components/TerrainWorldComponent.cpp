#include "PlutoGE/scene/components/TerrainWorldComponent.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"
#include "PlutoGE/scene/volume/VolumeEditArchive.h"
#include "PlutoGE/scene/volume/VolumeFile.h"

#include <filesystem>
#include <fstream>
#include <thread>

namespace PlutoGE::scene
{
    namespace
    {
        std::optional<glm::vec3> &EditorViewer()
        {
            static std::optional<glm::vec3> viewer;
            return viewer;
        }

        std::string ResolvePath(const std::string &reference)
        {
            auto resolved = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference);
            return resolved.empty() ? reference : resolved;
        }

        // A file's bytes, from a mounted content pack or the disk. False when it cannot be read (or is not there).
        bool ReadFileBytes(const std::string &path, std::vector<std::uint8_t> &bytes)
        {
            if (content::IsMounted(path))
            {
                std::string packed;
                if (!content::ReadFile(path, packed))
                    return false;
                bytes.assign(packed.begin(), packed.end());
                return true;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return false;
            bytes.assign(std::istreambuf_iterator<char>(input), {});
            return true;
        }

        // Reads and decodes a height file: what a worker thread runs.
        std::optional<TerrainHeightData> ReadHeightFile(const std::string &path, std::string *error)
        {
            std::vector<std::uint8_t> bytes;
            if (!ReadFileBytes(path, bytes))
            {
                if (error)
                    *error = "Could not read " + path + ".";
                return std::nullopt;
            }
            return TerrainHeightFile::Decode(bytes, error);
        }

        bool WriteFile(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
        {
            std::error_code ignored;
            if (path.has_parent_path())
                std::filesystem::create_directories(path.parent_path(), ignored);
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return static_cast<bool>(output);
        }

        // Writes a hole file, or removes it when there are no holes (tiles without holes have no file).
        bool WriteHoleFile(const std::filesystem::path &path, const TerrainHoleMask &holes)
        {
            std::error_code ignored;
            if (holes.Empty())
            {
                std::filesystem::remove(path, ignored);
                return !std::filesystem::exists(path, ignored);
            }
            const auto text = TerrainHoleFile::Encode(holes);
            return WriteFile(path, std::vector<std::uint8_t>(text.begin(), text.end()));
        }

        template <typename Future>
        bool IsDone(const Future &future)
        {
            return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        std::optional<glm::vec3> MainCameraPosition(const Scene &scene)
        {
            std::optional<glm::vec3> found;
            const auto walk = [&](auto &&self, const Entity *entity) -> void {
                if (found || !entity->IsActive())
                    return;
                if (const auto *camera = entity->GetComponent<CameraComponent>(); camera && camera->IsEnabled() && camera->IsMainCamera())
                {
                    found = entity->GetWorldPosition();
                    return;
                }
                for (const auto *child : entity->GetChildren())
                    self(self, child);
            };
            for (const auto *root : scene.GetRootEntities())
                walk(walk, root);
            return found;
        }

        float ParseFloat(const Property &property, float fallback)
        {
            try { return std::stof(property.value); } catch (...) { return fallback; }
        }

        int ParseInt(const Property &property, int fallback)
        {
            try { return std::stoi(property.value); } catch (...) { return fallback; }
        }

        std::string TileName(const char *kind, TerrainTileCoord tile)
        {
            return std::string(kind) + " " + std::to_string(tile.x) + "," + std::to_string(tile.z);
        }
    }

    TerrainWorldComponent::~TerrainWorldComponent()
    {
        // Pending work finishes before its results go; the scene may already be going, so tiles are not touched.
        for (auto &future : m_abandonedLoads)
            if (future.valid())
                future.wait();
        for (auto &future : m_abandonedGenerations)
            if (future.valid())
                future.wait();
    }

    void TerrainWorldComponent::SetEditorViewer(std::optional<glm::vec3> worldPosition) { EditorViewer() = worldPosition; }

    std::optional<glm::vec3> TerrainWorldComponent::GetViewer() const
    {
        if (m_viewerOverride)
            return m_viewerOverride;
        const auto *owner = GetOwner();
        const auto *scene = owner ? owner->GetScene() : nullptr;
        if (scene && !scene->IsRuntimeStarted())
            return m_streamInEditor ? EditorViewer() : std::nullopt;
        if (scene)
            if (auto camera = MainCameraPosition(*scene))
                return camera;
        return EditorViewer();
    }

    void TerrainWorldComponent::SetGrid(const TerrainWorldGrid &grid)
    {
        if (grid == m_grid)
            return;
        UnloadAll();
        ClearOverview();
        m_grid = grid;
    }

    void TerrainWorldComponent::SetTileFiles(std::string pattern)
    {
        if (pattern == m_tileFiles)
            return;
        UnloadAll();
        m_tileFiles = std::move(pattern);
    }

    void TerrainWorldComponent::SetOverviewFile(std::string file)
    {
        if (file == m_overviewFile)
            return;
        ClearOverview();
        m_overviewFile = std::move(file);
    }

    std::vector<Property> TerrainWorldComponent::Serialize() const
    {
        return {
            {"TilesX", PropertyType::Int, std::to_string(m_grid.tilesX)},
            {"TilesZ", PropertyType::Int, std::to_string(m_grid.tilesZ)},
            {"TileSamples", PropertyType::Int, std::to_string(m_grid.tileSamples)},
            {"CellSize", PropertyType::Float, std::to_string(m_grid.cellSize)},
            {"HeightScale", PropertyType::Float, std::to_string(m_grid.heightScale)},
            {"TileFiles", PropertyType::String, m_tileFiles},
            {"TilePrefab", PropertyType::String, m_tilePrefab},
            {"TileMaterial", PropertyType::String, m_tileMaterial},
            {"TileSkirtDepth", PropertyType::Float, std::to_string(m_tileSkirtDepth)},
            {"OverviewFile", PropertyType::String, m_overviewFile},
            {"OverviewMaterial", PropertyType::String, m_overviewMaterial},
            {"OverviewSkirtDepth", PropertyType::Float, std::to_string(m_overviewSkirtDepth)},
            {"LoadRadius", PropertyType::Float, std::to_string(m_streaming.loadRadius)},
            {"UnloadRadius", PropertyType::Float, std::to_string(m_streaming.unloadRadius)},
            {"MaxLoadsInFlight", PropertyType::Int, std::to_string(m_streaming.maxLoadsInFlight)},
            {"ActivationsPerFrame", PropertyType::Int, std::to_string(m_streaming.activationsPerFrame)},
            {"StreamInEditor", PropertyType::Bool, m_streamInEditor ? "true" : "false"},
        };
    }

    void TerrainWorldComponent::Deserialize(const std::vector<Property> &properties)
    {
        TerrainWorldGrid grid = m_grid;
        std::string tileFiles = m_tileFiles, overviewFile = m_overviewFile, tilePrefab = m_tilePrefab, tileMaterial = m_tileMaterial;
        std::string overviewMaterial = m_overviewMaterial;
        float tileSkirt = m_tileSkirtDepth, overviewSkirt = m_overviewSkirtDepth;
        for (const auto &property : properties)
        {
            if (property.name == "TilesX") grid.tilesX = std::clamp(ParseInt(property, grid.tilesX), 1, 1024);
            else if (property.name == "TilesZ") grid.tilesZ = std::clamp(ParseInt(property, grid.tilesZ), 1, 1024);
            else if (property.name == "TileSamples") grid.tileSamples = std::clamp(ParseInt(property, grid.tileSamples), 3, TerrainHeightFile::MaxDimension);
            else if (property.name == "CellSize") grid.cellSize = std::max(ParseFloat(property, grid.cellSize), 0.01f);
            else if (property.name == "HeightScale") grid.heightScale = std::max(ParseFloat(property, grid.heightScale), 0.01f);
            else if (property.name == "TileFiles") tileFiles = property.value;
            else if (property.name == "TilePrefab") tilePrefab = property.value;
            else if (property.name == "TileMaterial") tileMaterial = property.value;
            else if (property.name == "TileSkirtDepth") tileSkirt = std::max(ParseFloat(property, tileSkirt), 0.0f);
            else if (property.name == "OverviewFile") overviewFile = property.value;
            else if (property.name == "OverviewMaterial") overviewMaterial = property.value;
            else if (property.name == "OverviewSkirtDepth") overviewSkirt = std::max(ParseFloat(property, overviewSkirt), 0.0f);
            else if (property.name == "LoadRadius") m_streaming.loadRadius = std::max(ParseFloat(property, m_streaming.loadRadius), 0.0f);
            else if (property.name == "UnloadRadius") m_streaming.unloadRadius = std::max(ParseFloat(property, m_streaming.unloadRadius), 0.0f);
            else if (property.name == "MaxLoadsInFlight") m_streaming.maxLoadsInFlight = std::clamp(ParseInt(property, m_streaming.maxLoadsInFlight), 1, 64);
            else if (property.name == "ActivationsPerFrame") m_streaming.activationsPerFrame = std::clamp(ParseInt(property, m_streaming.activationsPerFrame), 1, 64);
            else if (property.name == "StreamInEditor") m_streamInEditor = property.value == "true" || property.value == "1";
        }
        // What tiles are built from changed: rebuild them.
        const bool rebuild = grid != m_grid || tileFiles != m_tileFiles || tilePrefab != m_tilePrefab || tileMaterial != m_tileMaterial ||
                             tileSkirt != m_tileSkirtDepth;
        const bool overviewChanged = rebuild || overviewFile != m_overviewFile || overviewMaterial != m_overviewMaterial || overviewSkirt != m_overviewSkirtDepth;
        if (rebuild)
            UnloadAll();
        if (overviewChanged)
            ClearOverview();
        m_grid = grid;
        m_tileFiles = tileFiles;
        m_tilePrefab = tilePrefab;
        m_tileMaterial = tileMaterial;
        m_tileSkirtDepth = tileSkirt;
        m_overviewFile = overviewFile;
        m_overviewMaterial = overviewMaterial;
        m_overviewSkirtDepth = overviewSkirt;
    }

    void TerrainWorldComponent::OnRemoved()
    {
        UnloadAll();
        ClearOverview();
    }

    void TerrainWorldComponent::OnEnabledChanged()
    {
        if (!IsEnabled())
        {
            UnloadAll();
            ClearOverview();
        }
    }

    void TerrainWorldComponent::Update(float deltaTime)
    {
        WatchTileSource(deltaTime);
        Stream(false);
    }

    void TerrainWorldComponent::WatchTileSource(float deltaTime)
    {
        // Edits to the tile template (in any inspector) reach the loaded tiles a moment later.
        m_sourceCheckTimer -= deltaTime;
        if (m_sourceCheckTimer > 0.0f || !GetOwner())
            return;
        m_sourceCheckTimer = 0.25f;
        const auto signature = MakeTileSource()->Signature();
        if (std::any_of(m_tiles.begin(), m_tiles.end(), [&](const auto &entry) {
                return entry.second.state == TerrainTileState::Active && entry.second.entity && entry.second.sourceSignature != signature;
            }))
            RefreshTiles();
    }

    Entity *TerrainWorldComponent::GetTileTemplate() const
    {
        const auto *owner = GetOwner();
        if (!owner)
            return nullptr;
        for (auto *child : owner->GetChildren())
            if (child && !child->IsTransient() && child->HasTag(std::string(TileTemplateTag)))
                return child;
        return nullptr;
    }

    bool TerrainWorldComponent::IsTileTemplate(const Entity &entity)
    {
        const auto *parent = entity.GetParent();
        return entity.HasTag(std::string(TileTemplateTag)) && parent && parent->GetComponent<TerrainWorldComponent>();
    }

    Entity *TerrainWorldComponent::EnsureTileTemplate()
    {
        if (auto *existing = GetTileTemplate())
            return existing;
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
            return nullptr;
        auto *entity = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Tile Template", .tags = {std::string(TileTemplateTag)}}), owner);
        // Inactive: it is a description of tiles, never drawn or collided with itself.
        entity->SetActive(false);
        auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 2, .depth = 2});
        terrain->Deserialize({{"MaterialAsset", PropertyType::String,
                               m_tileMaterial.empty() ? std::string("engine://builtin/material/default-shaded") : m_tileMaterial}});
        entity->CreateComponent<ColliderComponent>()->SetShape(ColliderShape::Terrain);
        return entity;
    }

    std::unique_ptr<TerrainTileSource> TerrainWorldComponent::MakeTileSource() const
    {
        if (!m_tilePrefab.empty())
            return std::make_unique<PrefabTileSource>(m_tilePrefab);
        if (const auto *tileTemplate = GetTileTemplate())
            return std::make_unique<TemplateTileSource>(*tileTemplate);
        return std::make_unique<DefaultTileSource>(m_tileMaterial);
    }

    void TerrainWorldComponent::RefreshTiles()
    {
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
            return;
        const auto source = MakeTileSource();
        const auto signature = source->Signature();
        for (auto &[coord, tile] : m_tiles)
        {
            auto *entity = tile.state == TerrainTileState::Active && tile.entity ? scene->FindEntityByID(tile.entity) : nullptr;
            if (!entity)
                continue;
            if (!source->Refresh(*entity))
            {
                // The source changed shape (components added or removed): build the tiles again.
                UnloadAll();
                break;
            }
            // Layers may have been added: painted weight files still name the tile's own.
            if (auto *terrain = entity->GetComponent<TerrainComponent>(); terrain && !terrain->GetLayers().Empty() &&
                                                                        terrain->GetLayers().GetWeightMapPaths()[0].empty())
                terrain->GetLayers().SetWeightMapPaths({TileWeightFile(coord, 0), TileWeightFile(coord, 1)});
            tile.sourceSignature = signature;
            StartGeneration(tile, *entity);
        }
    }

    TerrainEditSurface TerrainWorldComponent::GetEditSurface() const
    {
        std::vector<TerrainComponent *> terrains;
        for (const auto &[coord, tile] : m_tiles)
            if (auto *terrain = tile.state == TerrainTileState::Active ? GetTileTerrain(coord) : nullptr)
                terrains.push_back(terrain);
        return TerrainEditSurface(std::move(terrains));
    }

    std::optional<TerrainTileCoord> TerrainWorldComponent::FindTile(const Entity &entity) const
    {
        for (const auto &[coord, tile] : m_tiles)
            if (tile.state == TerrainTileState::Active && tile.entity == entity.GetID())
                return coord;
        return std::nullopt;
    }

    std::optional<TerrainTileCoord> TerrainWorldComponent::FindTile(const TerrainComponent &terrain) const
    {
        return terrain.GetOwner() ? FindTile(*terrain.GetOwner()) : std::nullopt;
    }

    std::string TerrainWorldComponent::TileSidecarFile(TerrainTileCoord tile, std::string_view suffix) const
    {
        auto file = TerrainTileFile(m_tileFiles, tile);
        constexpr std::string_view extension = TerrainHeightFile::Extension;
        if (file.size() >= extension.size() && file.compare(file.size() - extension.size(), extension.size(), extension) == 0)
            file.resize(file.size() - extension.size());
        return file.append(suffix);
    }

    std::string TerrainWorldComponent::TileWeightFile(TerrainTileCoord tile, std::size_t plane) const
    {
        return TileSidecarFile(tile, "_weights" + std::to_string(plane) + ".png");
    }

    std::string TerrainWorldComponent::TileHoleFile(TerrainTileCoord tile) const
    {
        return TileSidecarFile(tile, std::string("_holes").append(TerrainHoleFile::Extension));
    }

    std::string TerrainWorldComponent::TileVolumeFile(TerrainTileCoord tile) const
    {
        return TileSidecarFile(tile, std::string("_volume").append(volume::VolumeFile::Extension));
    }

    bool TerrainWorldComponent::SaveTileHoles(TerrainTileCoord tile)
    {
        const auto *terrain = GetTileTerrain(tile);
        return terrain && WriteHoleFile(ResolvePath(TileHoleFile(tile)), terrain->GetHoles());
    }

    bool TerrainWorldComponent::StoreTileHoles(TerrainTileCoord tile, const TerrainHoleMask &holes)
    {
        if (auto *terrain = GetTileTerrain(tile))
        {
            terrain->SetHoles(holes);
            return SaveTileHoles(tile);
        }
        return m_grid.Contains(tile) && WriteHoleFile(ResolvePath(TileHoleFile(tile)), holes);
    }

    bool TerrainWorldComponent::PatchTileHeights(TerrainTileCoord coord, const TerrainHeightRegion &region, TerrainComponent **loaded)
    {
        if (loaded)
            *loaded = nullptr;
        if (auto *terrain = GetTileTerrain(coord))
        {
            terrain->RestoreHeights(region);
            if (loaded)
                *loaded = terrain;
            return true;
        }
        const auto path = ResolvePath(TerrainTileFile(m_tileFiles, coord));
        std::string error;
        auto data = ReadHeightFile(path, &error);
        if (!data)
            return false;
        const int columns = region.rect.x1 - region.rect.x0;
        for (int z = region.rect.z0; z < region.rect.z1; ++z)
            for (int x = region.rect.x0; x < region.rect.x1; ++x)
                if (x >= 0 && z >= 0 && x < data->field.width && z < data->field.depth)
                    data->field.At(x, z) = region.samples[static_cast<std::size_t>(z - region.rect.z0) * columns + (x - region.rect.x0)];
        return WriteFile(path, TerrainHeightFile::Encode(*data));
    }

    std::vector<TerrainComponent *> TerrainWorldComponent::SyncTileEdges(const std::vector<TerrainTileCoord> &touched)
    {
        std::vector<TerrainComponent *> changedTerrains;
        auto *owner = GetOwner();
        if (!owner || touched.empty() || !m_grid.Valid())
            return changedTerrains;
        const int samples = m_grid.tileSamples;
        const int step = samples - 1;

        // The heights of every tile involved: the touched ones and their neighbours, loaded or read from their files.
        struct Heights
        {
            TerrainHeightData data;
            TerrainComponent *terrain = nullptr; // loaded
            bool touched = false;
        };
        std::map<TerrainTileCoord, Heights> tiles;
        for (const auto &centre : touched)
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const TerrainTileCoord coord{centre.x + dx, centre.z + dz};
                    if (!m_grid.Contains(coord) || tiles.contains(coord))
                        continue;
                    Heights heights;
                    if ((heights.terrain = GetTileTerrain(coord)))
                        heights.data = heights.terrain->GetHeightData();
                    else
                    {
                        std::string error;
                        auto data = ReadHeightFile(ResolvePath(TerrainTileFile(m_tileFiles, coord)), &error);
                        if (!data || data->field.width != samples || data->field.depth != samples)
                            continue;
                        heights.data = std::move(*data);
                    }
                    if (heights.data.border.size() != TerrainHeightBorder::Size(samples, samples))
                        heights.data.border = TerrainHeightBorder::Clamped(heights.data.field);
                    tiles.emplace(coord, std::move(heights));
                }
        for (const auto &coord : touched)
            if (auto found = tiles.find(coord); found != tiles.end())
                found->second.touched = true;

        // What the touched tiles now say a world sample is (their average where several hold it), read from a copy so
        // the values written below do not feed back.
        std::map<TerrainTileCoord, TerrainHeightField> edited;
        for (const auto &[coord, heights] : tiles)
            if (heights.touched)
                edited.emplace(coord, heights.data.field);
        const int worldX = m_grid.WorldSamplesX(), worldZ = m_grid.WorldSamplesZ();
        const auto touchedValue = [&](int gx, int gz) -> std::optional<float> {
            gx = std::clamp(gx, 0, worldX - 1);
            gz = std::clamp(gz, 0, worldZ - 1);
            float total = 0.0f;
            int count = 0;
            for (int tz = (gz - 1) / step; tz <= gz / step; ++tz)
                for (int tx = (gx - 1) / step; tx <= gx / step; ++tx)
                    if (const auto found = edited.find({tx, tz}); found != edited.end() && tx >= 0 && tz >= 0)
                    {
                        const int lx = gx - tx * step, lz = gz - tz * step;
                        if (lx >= 0 && lz >= 0 && lx < samples && lz < samples)
                            total += found->second.At(lx, lz), ++count;
                    }
            return count ? std::optional<float>(total / static_cast<float>(count)) : std::nullopt;
        };

        for (auto &[coord, heights] : tiles)
        {
            const auto before = heights.data;
            const int x0 = coord.x * step, z0 = coord.z * step;
            // Edge samples are shared with neighbours: they take what the touched tiles hold there.
            for (int z = 0; z < samples; ++z)
                for (int x = 0; x < samples; ++x)
                    if (x == 0 || z == 0 || x == samples - 1 || z == samples - 1)
                        if (const auto value = touchedValue(x0 + x, z0 + z))
                            heights.data.field.At(x, z) = *value;
            // The border ring holds neighbours' samples: those inside touched tiles are refreshed.
            for (int z = -TerrainHeightBorder::Width; z < samples + TerrainHeightBorder::Width; ++z)
                for (int x = -TerrainHeightBorder::Width; x < samples + TerrainHeightBorder::Width; ++x)
                    if (const auto index = TerrainHeightBorder::Index(samples, samples, x, z); index != TerrainHeightBorder::npos)
                        if (const auto value = touchedValue(x0 + x, z0 + z))
                            heights.data.border[index] = *value;
            if (heights.data == before)
                continue;
            if (heights.terrain)
            {
                heights.terrain->RestoreHeights({heights.terrain->GetSampleBounds(), heights.data.field.samples});
                heights.terrain->SetHeightBorder(heights.data.border);
                changedTerrains.push_back(heights.terrain);
            }
            else if (!WriteFile(ResolvePath(TerrainTileFile(m_tileFiles, coord)), TerrainHeightFile::Encode(heights.data)))
                ReportError("Could not update tile " + std::to_string(coord.x) + "," + std::to_string(coord.z) + "'s file.");
        }
        return changedTerrains;
    }

    void TerrainWorldComponent::ValidateEntities()
    {
        // Undo, prefab updates and scene reloads can remove transient entities behind this component's back.
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (!scene)
            return;
        // (Active tiles without an entity failed to load, and wait until the viewer leaves and comes back.)
        for (auto &[coord, tile] : m_tiles)
            if (tile.state == TerrainTileState::Active && tile.entity && !scene->FindEntityByID(tile.entity))
            {
                tile.state = TerrainTileState::Unloaded;
                tile.entity = 0;
                Abandon(tile);
            }
        for (auto it = m_overviewEntities.begin(); it != m_overviewEntities.end();)
            it = scene->FindEntityByID(it->second) ? std::next(it) : m_overviewEntities.erase(it);
    }

    void TerrainWorldComponent::Stream(bool unbudgeted)
    {
        auto *owner = GetOwner();
        if (!owner || !owner->GetScene() || !IsEnabled() || !m_grid.Valid())
            return;
        ValidateEntities();
        EnsureOverview();
        std::erase_if(m_abandonedLoads, [](const auto &future) { return !future.valid() || IsDone(future); });
        std::erase_if(m_abandonedGenerations, [](const auto &future) { return !future.valid() || IsDone(future); });

        // Finished reads become ready; ready tiles become terrains, nearest first, a few a frame.
        for (auto &[coord, tile] : m_tiles)
        {
            if (tile.state == TerrainTileState::Loading && IsDone(tile.load))
            {
                tile.data = tile.load.get();
                if (tile.data && !tile.data->error.empty())
                    ReportError(tile.data->error);
                if (tile.data)
                    tile.state = TerrainTileState::Ready;
                else
                {
                    // Keep the overview there; do not retry every frame.
                    ReportError("Tile " + std::to_string(coord.x) + "," + std::to_string(coord.z) + " did not load.");
                    tile.state = TerrainTileState::Active;
                    tile.entity = 0;
                }
            }
            if (tile.generating)
                PollGeneration(tile, false);
        }

        const auto viewer = GetViewer();
        if (!viewer)
            return;
        const glm::vec3 local3 = glm::vec3(glm::inverse(owner->GetWorldTransform()) * glm::vec4(*viewer, 1.0f));
        const glm::vec2 local(local3.x, local3.z);

        std::vector<std::pair<float, TerrainTileCoord>> ready;
        for (auto &[coord, tile] : m_tiles)
            if (tile.state == TerrainTileState::Ready)
                ready.push_back({m_grid.DistanceToTile(coord, local), coord});
        std::sort(ready.begin(), ready.end());
        const auto budget = unbudgeted ? ready.size() : static_cast<std::size_t>(std::max(m_streaming.activationsPerFrame, 1));
        for (std::size_t i = 0; i < ready.size() && i < budget; ++i)
            Activate(ready[i].second, m_tiles[ready[i].second]);

        const auto actions = PlanTerrainStreaming(m_grid, m_streaming, local, [this](TerrainTileCoord tile) {
            const auto found = m_tiles.find(tile);
            return found == m_tiles.end() ? TerrainTileState::Unloaded : found->second.state;
        });
        for (const auto &action : actions)
        {
            auto &tile = m_tiles[action.tile];
            if (action.kind == TerrainStreamingAction::Kind::Load)
                StartLoad(action.tile, tile);
            else
                Unload(action.tile, tile);
        }
    }

    void TerrainWorldComponent::StartLoad(TerrainTileCoord coord, Tile &tile)
    {
        // Resolved here: asset lookups belong to the main thread; the worker only reads and decodes.
        const auto path = ResolvePath(TerrainTileFile(m_tileFiles, coord));
        const auto holePath = ResolvePath(TileHoleFile(coord));
        const auto volumePath = ResolvePath(TileVolumeFile(coord));
        tile.load = std::async(std::launch::async, [path, holePath, volumePath]() -> std::optional<TileFiles> {
            std::string error;
            auto heights = ReadHeightFile(path, &error);
            if (!heights)
                return std::nullopt;
            TileFiles files{.heights = std::move(*heights)};
            // Most tiles have no hole file. One that is there but unusable is reported, and the tile loads without it.
            std::vector<std::uint8_t> bytes;
            if (ReadFileBytes(holePath, bytes))
            {
                const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                files.holes = TerrainHoleFile::Decode(text, files.heights.field.width - 1, files.heights.field.depth - 1, &error);
                if (!files.holes)
                    files.error = holePath + ": " + error;
            }
            // Read whether or not the tile has a volume: few tiles have a file, and its absence is cheap to learn here.
            if (std::vector<std::uint8_t> volumeBytes; ReadFileBytes(volumePath, volumeBytes))
                files.volume = std::move(volumeBytes);
            return files;
        });
        tile.state = TerrainTileState::Loading;
    }

    void TerrainWorldComponent::Activate(TerrainTileCoord coord, Tile &tile)
    {
        auto *owner = GetOwner();
        auto *scene = owner->GetScene();
        if (!tile.data)
        {
            tile.state = TerrainTileState::Unloaded;
            return;
        }

        std::string error;
        const auto source = MakeTileSource();
        tile.sourceSignature = source->Signature();
        Entity *entity = source->Create(*scene, *owner, &error);
        if (!entity)
        {
            ReportError("Tile source: " + error);
            entity = DefaultTileSource(m_tileMaterial).Create(*scene, *owner, &error);
        }
        entity->SetName(TileName("Tile", coord));
        entity->SetTransient(true);
        const glm::vec2 origin = m_grid.TileOrigin(coord);
        entity->SetLocalTransform(Transform{.position = {origin.x, 0.0f, origin.y}});

        auto *terrain = entity->GetComponent<TerrainComponent>();
        if (!terrain)
        {
            terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{});
            if (!entity->GetComponent<ColliderComponent>())
                entity->CreateComponent<ColliderComponent>()->SetShape(ColliderShape::Terrain);
        }
        // What belongs to the tile: the world's grid, the tile's heights, and its own weight files when it was painted.
        terrain->Deserialize({
            {"CellSize", PropertyType::Float, std::to_string(m_grid.cellSize)},
            {"HeightScale", PropertyType::Float, std::to_string(m_grid.heightScale)},
            {"SkirtDepth", PropertyType::Float, std::to_string(m_tileSkirtDepth)},
            {"Static", PropertyType::Bool, "true"},
        });
        terrain->SetHeightData(std::move(tile.data->heights));
        terrain->SetHeightDataPath(TerrainTileFile(m_tileFiles, coord));
        // The tile's own holes: none without a hole file, whatever the tile source carried.
        terrain->SetHoles(tile.data->holes ? std::move(*tile.data->holes) : TerrainHoleMask{});
        // A tile with a volume (its source has a TerrainVolumeComponent) keeps its stored chunks in its own file.
        if (auto *terrainVolume = entity->GetComponent<TerrainVolumeComponent>())
        {
            terrainVolume->SetVolumeFile(TileVolumeFile(coord));
            // Edits made since the file was read (the tile streamed out after digging) win over the file.
            if (const auto kept = m_volumeEdits.find(coord); kept != m_volumeEdits.end())
                terrainVolume->ProvideFileContents(kept->second);
            else
                terrainVolume->ProvideFileContents(std::move(tile.data->volume));
        }
        tile.data.reset();
        tile.paintedWeights = false;
        if (!terrain->GetLayers().Empty())
        {
            terrain->GetLayers().SetWeightMapPaths({TileWeightFile(coord, 0), TileWeightFile(coord, 1)});
            std::error_code ignored;
            if (std::filesystem::exists(ResolvePath(TileWeightFile(coord, 0)), ignored))
                tile.paintedWeights = terrain->GetLayers().LoadWeightMaps();
        }
        tile.entity = entity->GetID();
        tile.state = TerrainTileState::Active;
        StartGeneration(tile, *entity);
        if (const auto overview = m_overviewEntities.find(coord); overview != m_overviewEntities.end())
            if (auto *overviewEntity = scene->FindEntityByID(overview->second))
                overviewEntity->SetActive(false);
        InvalidateNavigation(coord);
    }

    void TerrainWorldComponent::StartGeneration(Tile &tile, Entity &entity)
    {
        // A procedural tile paints and plants itself on a worker; the result lands a few frames later. A painted tile
        // keeps its weights and is only planted.
        auto *procedural = entity.GetComponent<ProceduralTerrainComponent>();
        if (!procedural)
            return;
        procedural->SetGenerateOnLoad(false);
        procedural->SetTexture(!tile.paintedWeights);
        if (tile.generation.valid())
            m_abandonedGenerations.push_back(std::move(tile.generation));
        tile.generating = false;
        if (auto prepared = procedural->Prepare())
        {
            tile.generation = std::async(std::launch::async, [prepared = std::move(*prepared)] { return prepared.Run(); });
            tile.generating = true;
        }
    }

    void TerrainWorldComponent::PollGeneration(Tile &tile, bool wait)
    {
        if (!tile.generating || !tile.generation.valid())
            return;
        if (wait)
            tile.generation.wait();
        else if (!IsDone(tile.generation))
            return;
        auto result = tile.generation.get();
        tile.generating = false;
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        auto *entity = scene ? scene->FindEntityByID(tile.entity) : nullptr;
        if (auto *procedural = entity ? entity->GetComponent<ProceduralTerrainComponent>() : nullptr)
            procedural->Apply(result);
    }

    void TerrainWorldComponent::Abandon(Tile &tile)
    {
        if (tile.load.valid())
            m_abandonedLoads.push_back(std::move(tile.load));
        if (tile.generation.valid())
            m_abandonedGenerations.push_back(std::move(tile.generation));
        tile.generating = false;
        tile.data.reset();
    }

    void TerrainWorldComponent::Unload(TerrainTileCoord coord, Tile &tile)
    {
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        Abandon(tile);
        if (tile.entity && scene)
        {
            if (auto *entity = scene->FindEntityByID(tile.entity))
                KeepVolumeEdits(coord, *entity);
            scene->DestroyEntity(tile.entity);
            InvalidateNavigation(coord);
        }
        tile.entity = 0;
        tile.state = TerrainTileState::Unloaded;
        if (scene)
            if (const auto overview = m_overviewEntities.find(coord); overview != m_overviewEntities.end())
                if (auto *overviewEntity = scene->FindEntityByID(overview->second))
                    overviewEntity->SetActive(true);
    }

    // ---- terrain volume edits -------------------------------------------------------------------------------------------

    bool TerrainWorldComponent::StoreTileVolume(TerrainTileCoord coord, std::span<const std::uint8_t> edits)
    {
        if (auto *entity = GetTileEntity(coord))
        {
            auto *terrainVolume = entity->GetComponent<TerrainVolumeComponent>();
            return terrainVolume && terrainVolume->ImportEdits(edits) && terrainVolume->SaveFile();
        }
        auto contents = volume::VolumeFile::Decode(edits);
        if (!contents)
            return false;
        const std::filesystem::path path = ResolvePath(TileVolumeFile(coord));
        std::error_code ignored;
        if (contents->chunks.empty())
        {
            std::filesystem::remove(path, ignored);
            return true;
        }
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ignored);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(edits.data()), static_cast<std::streamsize>(edits.size()));
        return static_cast<bool>(output);
    }

    namespace
    {
        std::string TileKey(TerrainTileCoord tile) { return std::to_string(tile.x) + "," + std::to_string(tile.z); }

        std::optional<TerrainTileCoord> ParseTileKey(const std::string &key)
        {
            const auto comma = key.find(',');
            if (comma == std::string::npos)
                return std::nullopt;
            try
            {
                std::size_t used = 0;
                TerrainTileCoord tile{std::stoi(key.substr(0, comma), &used), 0};
                if (used != comma)
                    return std::nullopt;
                const auto rest = key.substr(comma + 1);
                tile.z = std::stoi(rest, &used);
                if (used != rest.size())
                    return std::nullopt;
                return tile;
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
    }

    void TerrainWorldComponent::KeepVolumeEdits(TerrainTileCoord coord, Entity &entity)
    {
        auto *terrainVolume = entity.GetComponent<TerrainVolumeComponent>();
        // A tile that was given kept edits keeps following them, even unchanged since (its file never had them).
        if (terrainVolume && (terrainVolume->HasUnsavedEdits() || m_volumeEdits.contains(coord)))
            if (auto bytes = terrainVolume->ExportEdits(); !bytes.empty())
                m_volumeEdits[coord] = std::move(bytes);
    }

    std::vector<std::uint8_t> TerrainWorldComponent::ExportVolumeEdits()
    {
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (scene)
            for (const auto &[coord, tile] : m_tiles)
                if (auto *entity = tile.entity ? scene->FindEntityByID(tile.entity) : nullptr)
                    KeepVolumeEdits(coord, *entity);
        volume::VolumeEditArchive::Entries entries;
        for (const auto &[coord, bytes] : m_volumeEdits)
            entries.emplace(TileKey(coord), bytes);
        return volume::VolumeEditArchive::Encode(entries);
    }

    bool TerrainWorldComponent::ImportVolumeEdits(std::span<const std::uint8_t> bytes, std::string *error)
    {
        auto entries = volume::VolumeEditArchive::Decode(bytes, error);
        if (!entries)
            return false;
        std::map<TerrainTileCoord, std::vector<std::uint8_t>> edits;
        for (auto &[key, data] : *entries)
        {
            const auto tile = ParseTileKey(key);
            if (!tile)
            {
                if (error)
                    *error = "The edit archive names a tile \"" + key + "\" that is not a tile coordinate.";
                return false;
            }
            edits.emplace(*tile, std::move(data));
        }
        m_volumeEdits = std::move(edits);
        // Loaded tiles change now; the others take theirs when they load.
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        for (const auto &[coord, tile] : m_tiles)
        {
            auto *entity = scene && tile.entity ? scene->FindEntityByID(tile.entity) : nullptr;
            auto *terrainVolume = entity ? entity->GetComponent<TerrainVolumeComponent>() : nullptr;
            if (!terrainVolume)
                continue;
            std::string reason;
            const auto kept = m_volumeEdits.find(coord);
            const bool ok = kept != m_volumeEdits.end() ? terrainVolume->ImportEdits(kept->second, &reason) : terrainVolume->ReloadFile(&reason);
            if (!ok)
                ReportError("Tile " + TileKey(coord) + " volume edits: " + reason);
        }
        return true;
    }

    void TerrainWorldComponent::ClearVolumeEdits()
    {
        ImportVolumeEdits(volume::VolumeEditArchive::Encode({}));
    }

    std::size_t TerrainWorldComponent::GetVolumeEditTileCount()
    {
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (scene)
            for (const auto &[coord, tile] : m_tiles)
                if (auto *entity = tile.entity ? scene->FindEntityByID(tile.entity) : nullptr)
                    KeepVolumeEdits(coord, *entity);
        return m_volumeEdits.size();
    }

    void TerrainWorldComponent::UnloadAll()
    {
        for (auto &[coord, tile] : m_tiles)
            Unload(coord, tile);
        m_tiles.clear();
    }

    void TerrainWorldComponent::Reload()
    {
        UnloadAll();
        ClearOverview();
        m_errors.clear();
    }

    void TerrainWorldComponent::EnsureOverview()
    {
        if (m_overviewTried || m_overviewFile.empty())
            return;
        m_overviewTried = true;
        std::string error;
        auto data = ReadHeightFile(ResolvePath(m_overviewFile), &error);
        if (!data)
        {
            ReportError("Overview: " + error);
            return;
        }
        m_overview = std::move(data->field);
        auto *owner = GetOwner();
        auto *scene = owner->GetScene();
        for (int z = 0; z < m_grid.tilesZ; ++z)
            for (int x = 0; x < m_grid.tilesX; ++x)
            {
                const TerrainTileCoord coord{x, z};
                auto part = OverviewTile(*m_overview, m_grid, coord);
                if (!part.Valid())
                {
                    ReportError("The overview does not divide into the world's tiles.");
                    return;
                }
                auto *entity = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = TileName("Overview", coord)}), owner);
                entity->SetTransient(true);
                const glm::vec2 origin = m_grid.TileOrigin(coord);
                entity->SetLocalTransform(Transform{.position = {origin.x, 0.0f, origin.y}});
                const int cells = part.width - 1;
                auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{});
                std::vector<Property> settings{
                    {"CellSize", PropertyType::Float, std::to_string(m_grid.TileSize() / static_cast<float>(cells))},
                    {"HeightScale", PropertyType::Float, std::to_string(m_grid.heightScale)},
                    {"ChunkSize", PropertyType::Int, std::to_string(std::max(cells, 2))},
                    {"LodCount", PropertyType::Int, "1"},
                    {"SurfaceSmoothing", PropertyType::Float, "0"},
                    {"SkirtDepth", PropertyType::Float, std::to_string(m_overviewSkirtDepth)},
                    {"Static", PropertyType::Bool, "true"},
                };
                const auto &material = !m_overviewMaterial.empty() ? m_overviewMaterial : m_tileMaterial;
                if (!material.empty())
                    settings.push_back({"MaterialAsset", PropertyType::String, material});
                terrain->Deserialize(settings);
                terrain->SetHeightData({std::move(part), {}});
                // Under a loaded tile the detailed terrain draws instead.
                if (const auto found = m_tiles.find(coord); found != m_tiles.end() && found->second.state == TerrainTileState::Active && found->second.entity)
                    entity->SetActive(false);
                m_overviewEntities[coord] = entity->GetID();
            }
    }

    void TerrainWorldComponent::ClearOverview()
    {
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        for (const auto &[coord, id] : m_overviewEntities)
            if (scene)
                scene->DestroyEntity(id);
        m_overviewEntities.clear();
        m_overview.reset();
        m_overviewTried = false;
    }

    void TerrainWorldComponent::InvalidateNavigation(TerrainTileCoord coord) const
    {
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
            return;
        const glm::mat4 world = owner->GetWorldTransform();
        const glm::vec2 origin = m_grid.TileOrigin(coord);
        const glm::vec3 a = glm::vec3(world * glm::vec4(origin.x, 0.0f, origin.y, 1.0f));
        const glm::vec3 b = glm::vec3(world * glm::vec4(origin.x + m_grid.TileSize(), m_grid.heightScale, origin.y + m_grid.TileSize(), 1.0f));
        InvalidateNavigationRegion(*scene, glm::min(a, b), glm::max(a, b));
    }

    void TerrainWorldComponent::ReportError(std::string message)
    {
        m_errors.push_back(std::move(message));
        if (m_errors.size() > 16)
            m_errors.erase(m_errors.begin());
    }

    TerrainTileState TerrainWorldComponent::GetTileState(TerrainTileCoord tile) const
    {
        const auto found = m_tiles.find(tile);
        return found == m_tiles.end() ? TerrainTileState::Unloaded : found->second.state;
    }

    Entity *TerrainWorldComponent::GetTileEntity(TerrainTileCoord tile) const
    {
        const auto found = m_tiles.find(tile);
        const auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        return found != m_tiles.end() && found->second.entity && scene ? const_cast<Scene *>(scene)->FindEntityByID(found->second.entity) : nullptr;
    }

    TerrainComponent *TerrainWorldComponent::GetTileTerrain(TerrainTileCoord tile) const
    {
        auto *entity = GetTileEntity(tile);
        return entity ? entity->GetComponent<TerrainComponent>() : nullptr;
    }

    Entity *TerrainWorldComponent::GetOverviewEntity(TerrainTileCoord tile) const
    {
        const auto found = m_overviewEntities.find(tile);
        const auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        return found != m_overviewEntities.end() && scene ? const_cast<Scene *>(scene)->FindEntityByID(found->second) : nullptr;
    }

    std::size_t TerrainWorldComponent::GetActiveTileCount() const
    {
        return static_cast<std::size_t>(std::count_if(m_tiles.begin(), m_tiles.end(), [](const auto &entry) {
            return entry.second.state == TerrainTileState::Active && entry.second.entity != 0;
        }));
    }

    bool TerrainWorldComponent::LoadAround(glm::vec3 worldPosition, std::chrono::milliseconds timeout)
    {
        const auto previous = m_viewerOverride;
        m_viewerOverride = worldPosition;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        bool complete = false;
        while (std::chrono::steady_clock::now() < deadline)
        {
            Stream(true);
            for (auto &[coord, tile] : m_tiles)
                if (tile.generating)
                    PollGeneration(tile, true);
            const auto *owner = GetOwner();
            if (!owner)
                break;
            const glm::vec3 local3 = glm::vec3(glm::inverse(owner->GetWorldTransform()) * glm::vec4(worldPosition, 1.0f));
            complete = true;
            for (int z = 0; z < m_grid.tilesZ && complete; ++z)
                for (int x = 0; x < m_grid.tilesX && complete; ++x)
                    if (m_grid.DistanceToTile({x, z}, {local3.x, local3.z}) <= m_streaming.loadRadius)
                    {
                        const auto found = m_tiles.find({x, z});
                        complete = found != m_tiles.end() && found->second.state == TerrainTileState::Active && !found->second.generating;
                    }
            if (complete)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        m_viewerOverride = previous;
        return complete;
    }

    std::optional<float> TerrainWorldComponent::SampleHeight(glm::vec3 worldPosition) const
    {
        const auto *owner = GetOwner();
        if (!owner)
            return std::nullopt;
        const glm::mat4 world = owner->GetWorldTransform();
        const glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(worldPosition, 1.0f));
        const auto tile = m_grid.TileAt({local.x, local.z});
        if (!tile)
            return std::nullopt;
        if (const auto *terrain = GetTileTerrain(*tile))
        {
            float height = 0.0f;
            glm::vec3 normal{};
            if (terrain->TrySampleWorldSurface(worldPosition, height, normal))
                return height;
        }
        if (m_overview && m_overview->Valid())
        {
            const glm::vec2 extent = m_grid.Extent();
            const float h = m_overview->Sample(local.x / extent.x * static_cast<float>(m_overview->width - 1),
                                               local.z / extent.y * static_cast<float>(m_overview->depth - 1));
            return glm::vec3(world * glm::vec4(local.x, h, local.z, 1.0f)).y;
        }
        return std::nullopt;
    }

    TerrainWorldComponent::WriteResult TerrainWorldComponent::WriteWorldFiles(const TerrainHeightField &heights, const TerrainWorldGrid &grid,
                                                                             const std::string &tilePathPattern, const std::string &overviewPath,
                                                                             int overviewSamplesPerTile, TerrainHeightEncoding encoding)
    {
        WriteResult result;
        if (!heights.Valid() || !grid.Valid())
        {
            result.error = "Nothing to write: the heights or the grid are not valid.";
            return result;
        }
        const TerrainHeightField world = heights.width == grid.WorldSamplesX() && heights.depth == grid.WorldSamplesZ()
                                             ? heights
                                             : ResampleHeightField(heights, grid.WorldSamplesX(), grid.WorldSamplesZ());
        for (int z = 0; z < grid.tilesZ; ++z)
            for (int x = 0; x < grid.tilesX; ++x)
            {
                const auto path = TerrainTileFile(tilePathPattern, {x, z});
                if (!WriteFile(path, TerrainHeightFile::Encode(ExtractTerrainTile(world, grid, {x, z}), encoding)))
                {
                    result.error = "Could not write " + path + ".";
                    return result;
                }
                ++result.tilesWritten;
            }
        if (!overviewPath.empty())
        {
            TerrainHeightData overview;
            overview.field = BuildTerrainOverview(world, grid, overviewSamplesPerTile);
            if (!WriteFile(overviewPath, TerrainHeightFile::Encode(overview, encoding)))
                result.error = "Could not write " + overviewPath + ".";
        }
        return result;
    }
}
