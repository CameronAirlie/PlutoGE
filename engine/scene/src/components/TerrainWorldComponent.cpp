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

        // Reads and decodes a height file: what a worker thread runs.
        std::optional<TerrainHeightData> ReadHeightFile(const std::string &path, std::string *error)
        {
            std::vector<std::uint8_t> bytes;
            if (content::IsMounted(path))
            {
                std::string packed;
                if (!content::ReadFile(path, packed))
                {
                    if (error)
                        *error = "Could not read " + path + ".";
                    return std::nullopt;
                }
                bytes.assign(packed.begin(), packed.end());
            }
            else
            {
                std::ifstream input(path, std::ios::binary);
                if (!input)
                {
                    if (error)
                        *error = "Could not read " + path + ".";
                    return std::nullopt;
                }
                bytes.assign(std::istreambuf_iterator<char>(input), {});
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

    void TerrainWorldComponent::Update(float)
    {
        Stream(false);
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
        tile.load = std::async(std::launch::async, [path] {
            std::string error;
            return ReadHeightFile(path, &error);
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

        Entity *entity = nullptr;
        if (!m_tilePrefab.empty())
        {
            std::string error;
            entity = Prefab::Instantiate(*scene, m_tilePrefab, owner, &error);
            if (!entity)
                ReportError("Tile prefab " + m_tilePrefab + ": " + error);
        }
        if (!entity)
            entity = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = TileName("Tile", coord)}), owner);
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
        std::vector<Property> settings{
            {"CellSize", PropertyType::Float, std::to_string(m_grid.cellSize)},
            {"HeightScale", PropertyType::Float, std::to_string(m_grid.heightScale)},
            {"SkirtDepth", PropertyType::Float, std::to_string(m_tileSkirtDepth)},
            {"Static", PropertyType::Bool, "true"},
        };
        if (!m_tileMaterial.empty() && m_tilePrefab.empty())
            settings.push_back({"MaterialAsset", PropertyType::String, m_tileMaterial});
        terrain->Deserialize(settings);
        terrain->SetHeightData(std::move(*tile.data));
        terrain->SetHeightDataPath(TerrainTileFile(m_tileFiles, coord));
        tile.data.reset();
        tile.entity = entity->GetID();
        tile.state = TerrainTileState::Active;

        // A procedural tile paints and plants itself on a worker; the result lands a few frames later.
        if (auto *procedural = entity->GetComponent<ProceduralTerrainComponent>())
        {
            procedural->SetGenerateOnLoad(false);
            if (auto prepared = procedural->Prepare())
            {
                tile.generation = std::async(std::launch::async, [prepared = std::move(*prepared)] { return prepared.Run(); });
                tile.generating = true;
            }
        }
        if (const auto overview = m_overviewEntities.find(coord); overview != m_overviewEntities.end())
            if (auto *overviewEntity = scene->FindEntityByID(overview->second))
                overviewEntity->SetActive(false);
        InvalidateNavigation(coord);
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

    void TerrainWorldComponent::UnloadAll()
    {
        for (auto &[coord, tile] : m_tiles)
            Unload(coord, tile);
        m_tiles.clear();
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
        const auto walk = [&](auto &&self, Entity *entity) -> void {
            for (auto *navigation : entity->GetComponents<NavigationMeshComponent>())
                if (navigation->IsEnabled())
                    navigation->InvalidateRegion(glm::min(a, b), glm::max(a, b));
            for (auto *child : entity->GetChildren())
                self(self, child);
        };
        for (auto *root : scene->GetRootEntities())
            walk(walk, root);
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
