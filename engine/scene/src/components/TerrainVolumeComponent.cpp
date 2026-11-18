#include "PlutoGE/scene/components/TerrainVolumeComponent.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/volume/VolumeFile.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>

namespace PlutoGE::scene
{
    namespace
    {
        // The holes are recomputed on the next update.
        constexpr std::uint64_t kHolesStale = std::numeric_limits<std::uint64_t>::max();

        std::string ResolvePath(const std::string &reference)
        {
            auto resolved = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference);
            return resolved.empty() ? reference : resolved;
        }

        std::optional<std::vector<std::uint8_t>> ReadFileIfPresent(const std::string &path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return std::nullopt;
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
        }

        float ParseFloat(const std::string &text, float fallback)
        {
            try { return std::stof(text); } catch (...) { return fallback; }
        }

        int ParseInt(const std::string &text, int fallback)
        {
            try { return std::stoi(text); } catch (...) { return fallback; }
        }

        std::string ChunkName(volume::VolumeChunkCoord chunk)
        {
            return "Volume Chunk " + std::to_string(chunk.x) + "," + std::to_string(chunk.y) + "," + std::to_string(chunk.z);
        }
    }

    TerrainVolumeComponent::TerrainVolumeComponent()
        : m_queue(std::make_unique<volume::VolumeMeshQueue>(std::make_shared<volume::SurfaceNetsMesher>()))
    {
    }

    TerrainVolumeComponent::~TerrainVolumeComponent() = default;

    // ---- settings ------------------------------------------------------------------------------------------------------

    void TerrainVolumeComponent::SetVoxelSize(float metres)
    {
        metres = std::clamp(metres, 0.05f, 8.0f);
        if (metres == m_voxelSize)
            return;
        m_voxelSize = metres;
        ResetVolume();
    }

    void TerrainVolumeComponent::SetChunkVoxels(int samples)
    {
        samples = std::clamp(samples, 8, 64);
        if (samples == m_chunkVoxels)
            return;
        m_chunkVoxels = samples;
        ResetVolume();
    }

    void TerrainVolumeComponent::SetMaterialAsset(std::string asset)
    {
        if (asset == m_materialAsset)
            return;
        m_materialAsset = std::move(asset);
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (!scene)
            return;
        auto *material = ChunkMaterial();
        for (const auto &[chunk, id] : m_chunkEntities)
            if (auto *entity = scene->FindEntityByID(id))
                if (auto *mesh = entity->GetComponent<MeshComponent>())
                    mesh->SetMaterial(material);
    }

    void TerrainVolumeComponent::SetVolumeFile(std::string file)
    {
        if (file == m_volumeFile)
            return;
        m_volumeFile = std::move(file);
        // Another file is other stored chunks: start again from it.
        ResetVolume();
    }

    void TerrainVolumeComponent::SetCollision(bool collision)
    {
        if (collision == m_collision)
            return;
        m_collision = collision;
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (!scene)
            return;
        for (const auto &[chunk, id] : m_chunkEntities)
        {
            auto *entity = scene->FindEntityByID(id);
            if (!entity)
                continue;
            auto *collider = entity->GetComponent<ColliderComponent>();
            if (m_collision && !collider)
                entity->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Mesh});
            else if (!m_collision && collider)
                entity->RemoveComponent(collider);
        }
        scene->MarkColliderShapesDirty();
    }

    void TerrainVolumeComponent::SetCoupling(const volume::TerrainCouplingSettings &settings)
    {
        if (settings == m_coupling)
            return;
        m_coupling = settings;
        // Every fitted mesh and the holes depend on these.
        for (const auto &[chunk, id] : m_chunkEntities)
            m_toMesh.insert(chunk);
        m_holesVolumeRevision = kHolesStale;
    }

    std::vector<Property> TerrainVolumeComponent::Serialize() const
    {
        return {
            {"VoxelSize", PropertyType::Float, std::to_string(m_voxelSize)},
            {"ChunkVoxels", PropertyType::Int, std::to_string(m_chunkVoxels)},
            {"MaterialAsset", PropertyType::String, m_materialAsset},
            {"VolumeFile", PropertyType::String, m_volumeFile},
            {"Collision", PropertyType::Bool, m_collision ? "true" : "false"},
            {"MeshesPerFrame", PropertyType::Int, std::to_string(m_meshesPerFrame)},
            {"BreachTolerance", PropertyType::Float, std::to_string(m_coupling.breachTolerance)},
            {"SurfaceTolerance", PropertyType::Float, std::to_string(m_coupling.surfaceTolerance)},
            {"OverlapCells", PropertyType::Int, std::to_string(m_coupling.overlapCells)},
            {"OverlapSink", PropertyType::Float, std::to_string(m_coupling.overlapSink)},
        };
    }

    void TerrainVolumeComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto coupling = m_coupling;
        for (const auto &property : properties)
        {
            const auto &text = property.Text();
            if (property.name == "VoxelSize")
                SetVoxelSize(ParseFloat(text, m_voxelSize));
            else if (property.name == "ChunkVoxels")
                SetChunkVoxels(ParseInt(text, m_chunkVoxels));
            else if (property.name == "MaterialAsset")
                SetMaterialAsset(text);
            else if (property.name == "VolumeFile")
                SetVolumeFile(text);
            else if (property.name == "Collision")
                SetCollision(text == "true" || text == "1");
            else if (property.name == "MeshesPerFrame")
                SetMeshesPerFrame(ParseInt(text, m_meshesPerFrame));
            else if (property.name == "BreachTolerance")
                coupling.breachTolerance = std::max(ParseFloat(text, coupling.breachTolerance), 0.0f);
            else if (property.name == "SurfaceTolerance")
                coupling.surfaceTolerance = std::max(ParseFloat(text, coupling.surfaceTolerance), 0.0f);
            else if (property.name == "OverlapCells")
                coupling.overlapCells = std::clamp(ParseInt(text, coupling.overlapCells), 0, 4);
            else if (property.name == "OverlapSink")
                coupling.overlapSink = std::max(ParseFloat(text, coupling.overlapSink), 0.0f);
        }
        SetCoupling(coupling);
    }

    // ---- editing -------------------------------------------------------------------------------------------------------

    volume::VolumeEditResult TerrainVolumeComponent::Apply(const volume::VolumeShape &worldShape, const volume::VolumeEdit &edit)
    {
        const auto *owner = GetOwner();
        if (!owner)
            return {};
        // The volume asks about positions in the terrain's space; the shape answers in world space.
        const volume::TransformedShape local(worldShape, owner->GetWorldTransform());
        return ApplyLocal(local, edit);
    }

    volume::VolumeEditResult TerrainVolumeComponent::ApplyLocal(const volume::VolumeShape &localShape, const volume::VolumeEdit &edit)
    {
        if (!EnsureVolume())
        {
            m_lastError = "A terrain volume needs a terrain beside it.";
            return {};
        }
        return m_volume->Apply(localShape, edit);
    }

    volume::TerrainVolume *TerrainVolumeComponent::GetVolume()
    {
        return EnsureVolume() ? m_volume.get() : nullptr;
    }

    // ---- files ---------------------------------------------------------------------------------------------------------

    bool TerrainVolumeComponent::SaveFile(std::string *error)
    {
        const auto fail = [&](std::string reason) {
            m_lastError = reason;
            if (error)
                *error = std::move(reason);
            return false;
        };
        if (m_volumeFile.empty())
            return fail("The terrain volume has no volume file.");
        if (!m_volume)
            return true; // nothing was loaded or edited: the file is as it was
        const std::filesystem::path path = ResolvePath(m_volumeFile);
        std::error_code ignored;
        if (m_volume->GetStoredChunkCount() == 0)
        {
            std::filesystem::remove(path, ignored);
            return true;
        }
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ignored);
        const auto bytes = volume::VolumeFile::Encode(*m_volume);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output)
            return fail("Could not write " + path.string() + ".");
        return true;
    }

    void TerrainVolumeComponent::ProvideFileContents(std::optional<std::vector<std::uint8_t>> contents)
    {
        m_providedFile.emplace(std::move(contents));
        // A volume made before the contents arrived takes them now.
        if (m_volume)
        {
            ResetVolume();
        }
    }

    void TerrainVolumeComponent::LoadStoredChunks()
    {
        std::optional<std::vector<std::uint8_t>> bytes;
        if (m_providedFile)
            bytes = std::move(*m_providedFile);
        else if (!m_volumeFile.empty())
            bytes = ReadFileIfPresent(ResolvePath(m_volumeFile));
        m_providedFile.reset();
        if (!bytes)
            return;
        std::string error;
        if (!volume::VolumeFile::Load(*bytes, *m_volume, &error))
            m_lastError = m_volumeFile + ": " + error;
    }

    // ---- the volume ------------------------------------------------------------------------------------------------------

    TerrainComponent *TerrainVolumeComponent::FindTerrain() const
    {
        auto *owner = GetOwner();
        return owner ? owner->GetComponent<TerrainComponent>() : nullptr;
    }

    std::shared_ptr<const volume::TerrainSurface> TerrainVolumeComponent::SnapshotSurface(const TerrainComponent &terrain) const
    {
        const int width = terrain.GetWidth(), depth = terrain.GetDepth();
        std::vector<float> heights(static_cast<std::size_t>(width) * depth);
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
                heights[static_cast<std::size_t>(z) * width + x] = terrain.GetSurfaceSampleHeight(x, z);
        return std::make_shared<const volume::TerrainSurface>(width, depth, terrain.GetCellSize(), std::move(heights));
    }

    bool TerrainVolumeComponent::EnsureVolume()
    {
        if (m_volume)
            return true;
        auto *terrain = FindTerrain();
        if (!terrain)
            return false;
        m_surface = SnapshotSurface(*terrain);
        m_surfaceHeightsRevision = terrain->GetHeightsRevision();
        volume::VolumeGrid grid;
        grid.voxelSize = m_voxelSize;
        grid.chunkVoxels = m_chunkVoxels;
        m_volume = std::make_unique<volume::TerrainVolume>(grid, volume::MakeTerrainDensity(m_surface));
        m_volume->SetMeshApron(m_queue->GetMesher().Apron());
        m_holes = std::make_shared<const TerrainHoleMask>();
        m_pendingHoles.reset();
        m_holesVolumeRevision = kHolesStale;
        LoadStoredChunks();
        return true;
    }

    void TerrainVolumeComponent::ResetVolume()
    {
        m_queue->Clear();
        m_toMesh.clear();
        DestroyChunkEntities();
        if (auto *terrain = FindTerrain())
            terrain->SetDerivedHoles(HoleSource, TerrainHoleMask{});
        m_volume.reset();
        m_surface.reset();
        m_holes.reset();
        m_pendingHoles.reset();
        m_holesVolumeRevision = kHolesStale;
    }

    // ---- updating ------------------------------------------------------------------------------------------------------

    void TerrainVolumeComponent::Update(float)
    {
        if (IsEnabled())
            Step(false);
    }

    void TerrainVolumeComponent::OnRemoved()
    {
        ResetVolume();
    }

    void TerrainVolumeComponent::OnEnabledChanged()
    {
        if (IsEnabled() || !m_volume)
            return;
        // Disabled: the terrain shows its own ground again. The volume and its edits stay; enabling meshes them again.
        m_queue->Clear();
        m_toMesh.clear();
        DestroyChunkEntities();
        if (auto *terrain = FindTerrain())
            terrain->SetDerivedHoles(HoleSource, TerrainHoleMask{});
        m_holes = std::make_shared<const TerrainHoleMask>();
        m_pendingHoles.reset();
        m_holesVolumeRevision = kHolesStale;
        for (const auto chunk : m_volume->GetStoredChunks())
            for (int dz = -1; dz <= 1; ++dz)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        m_toMesh.insert({chunk.x + dx, chunk.y + dy, chunk.z + dz});
    }

    bool TerrainVolumeComponent::IsUpToDate() const
    {
        if (!m_volume)
            return true;
        const auto *terrain = FindTerrain();
        return m_toMesh.empty() && m_queue->Idle() && !m_pendingHoles && m_volume->GetDirtyChunks().empty() &&
               m_holesVolumeRevision == m_volume->GetRevision() && (!terrain || terrain->GetHeightsRevision() == m_surfaceHeightsRevision);
    }

    void TerrainVolumeComponent::Flush()
    {
        // The volume (and its file) first: a component that has not updated yet has nothing pending, only unread.
        if (IsEnabled())
            EnsureVolume();
        // A step can find more to do (holes that changed call for their neighbours' meshes): a few rounds settle it.
        for (int round = 0; round < 4 && IsEnabled() && !IsUpToDate(); ++round)
            Step(true);
    }

    void TerrainVolumeComponent::Step(bool unbudgeted)
    {
        auto *terrain = FindTerrain();
        if (!terrain || !EnsureVolume())
            return;
        ValidateChunkEntities();
        FollowTerrainHeights(*terrain);
        for (const auto chunk : m_volume->TakeDirtyChunks())
            m_toMesh.insert(chunk);
        RefreshHoles();
        const std::size_t budget = unbudgeted ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(m_meshesPerFrame);
        ScheduleMeshing(budget);
        ApplyMeshes(m_queue->Collect(budget, unbudgeted));
        ApplyHolesWhenMeshed(*terrain);
    }

    void TerrainVolumeComponent::FollowTerrainHeights(TerrainComponent &terrain)
    {
        if (terrain.GetHeightsRevision() == m_surfaceHeightsRevision)
            return;
        // Sculpted: unstored chunks follow the new surface; every mesh and the holes are made again.
        m_surface = SnapshotSurface(terrain);
        m_surfaceHeightsRevision = terrain.GetHeightsRevision();
        m_volume->SetBase(volume::MakeTerrainDensity(m_surface));
        for (const auto &[chunk, id] : m_chunkEntities)
            m_toMesh.insert(chunk);
        m_holesVolumeRevision = kHolesStale;
    }

    void TerrainVolumeComponent::RefreshHoles()
    {
        if (m_holesVolumeRevision == m_volume->GetRevision())
            return;
        m_holesVolumeRevision = m_volume->GetRevision();
        auto holes = volume::ComputeVolumeHoles(*m_volume, *m_surface, m_coupling);
        const auto &target = m_pendingHoles ? *m_pendingHoles : *m_holes;
        if (holes == target)
            return;
        // Cells whose cut changes need their surface (and its ring) meshed again, edited or not.
        for (const auto chunk : volume::ChunksAlongSurface(m_volume->GetGrid(), *m_surface, volume::ChangedCells(target, holes), m_coupling))
            m_toMesh.insert(chunk);
        m_pendingHoles = std::make_shared<const TerrainHoleMask>(std::move(holes));
    }

    void TerrainVolumeComponent::ScheduleMeshing(std::size_t budget)
    {
        if (m_toMesh.empty())
            return;
        // Fitted against the holes that will be on the terrain when these meshes land.
        auto finish = [surface = m_surface, holes = m_pendingHoles ? m_pendingHoles : m_holes, coupling = m_coupling,
                       grid = m_volume->GetGrid()](volume::VolumeChunkCoord chunk, volume::VolumeMesh &mesh) {
            volume::FitVolumeMeshToTerrain(mesh, grid.ChunkOrigin(chunk), *surface, *holes, coupling, grid.voxelSize);
        };
        for (std::size_t scheduled = 0; scheduled < budget && !m_toMesh.empty(); ++scheduled)
        {
            const auto chunk = *m_toMesh.begin();
            m_toMesh.erase(m_toMesh.begin());
            m_queue->Enqueue(*m_volume, chunk, finish);
        }
    }

    void TerrainVolumeComponent::ApplyMeshes(std::vector<volume::VolumeMeshQueue::Result> results)
    {
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene || results.empty())
            return;
        const auto &grid = m_volume->GetGrid();
        glm::vec3 changedMin(std::numeric_limits<float>::max()), changedMax(std::numeric_limits<float>::lowest());
        for (auto &[chunk, mesh] : results)
        {
            changedMin = glm::min(changedMin, grid.ChunkOrigin(chunk));
            changedMax = glm::max(changedMax, grid.ChunkOrigin(chunk) + grid.ChunkSize());
            auto *entity = GetChunkEntity(chunk);
            if (mesh.Empty())
            {
                if (entity)
                    scene->DestroyEntity(entity->GetID());
                m_chunkEntities.erase(chunk);
                continue;
            }
            if (!entity)
            {
                auto created = std::make_unique<Entity>(EntityConfig{.name = ChunkName(chunk)});
                created->SetTransient(true);
                created->SetLocalTransform(Transform{.position = grid.ChunkOrigin(chunk)});
                for (const auto &tag : owner->GetTags())
                    created->AddTag(tag);
                created->AddTag(std::string(ChunkTag));
                auto *meshComponent = created->CreateComponent<MeshComponent>(MeshComponentConfig{});
                meshComponent->SetMaterial(ChunkMaterial());
                meshComponent->SetStatic(true);
                if (m_collision)
                    created->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Mesh});
                entity = scene->AddEntity(std::move(created), owner);
                m_chunkEntities[chunk] = entity->GetID();
            }
            if (auto *meshComponent = entity->GetComponent<MeshComponent>())
                meshComponent->SetRuntimeGeometry(std::move(mesh.mesh));
        }
        // Mesh colliders rebuild from the new geometry before queries later this frame.
        scene->MarkColliderShapesDirty();
        InvalidateNavigation(changedMin, changedMax);
    }

    void TerrainVolumeComponent::ApplyHolesWhenMeshed(TerrainComponent &terrain)
    {
        // The terrain opens only once the meshes filling its new holes are in (and closes once those that filled the
        // old ones are gone), so no frame shows a hole with nothing in it.
        if (!m_pendingHoles || !m_toMesh.empty() || !m_queue->Idle())
            return;
        terrain.SetDerivedHoles(HoleSource, *m_pendingHoles);
        m_holes = std::move(m_pendingHoles);
        m_pendingHoles.reset();
    }

    // ---- chunk entities ------------------------------------------------------------------------------------------------

    Entity *TerrainVolumeComponent::GetChunkEntity(volume::VolumeChunkCoord chunk) const
    {
        const auto found = m_chunkEntities.find(chunk);
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        return found != m_chunkEntities.end() && scene ? scene->FindEntityByID(found->second) : nullptr;
    }

    void TerrainVolumeComponent::ValidateChunkEntities()
    {
        // Undo, prefab updates and reloads can remove transient entities behind the component's back: mesh them again.
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        for (auto it = m_chunkEntities.begin(); it != m_chunkEntities.end();)
        {
            if (scene && scene->FindEntityByID(it->second))
            {
                ++it;
                continue;
            }
            m_toMesh.insert(it->first);
            it = m_chunkEntities.erase(it);
        }
    }

    void TerrainVolumeComponent::DestroyChunkEntities()
    {
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (scene)
            for (const auto &[chunk, id] : m_chunkEntities)
                scene->DestroyEntity(id);
        m_chunkEntities.clear();
    }

    void TerrainVolumeComponent::InvalidateNavigation(const glm::vec3 &localMin, const glm::vec3 &localMax) const
    {
        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene || glm::any(glm::greaterThan(localMin, localMax)))
            return;
        const glm::mat4 world = owner->GetWorldTransform();
        glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner)
        {
            const glm::vec3 local((corner & 1) ? localMax.x : localMin.x, (corner & 2) ? localMax.y : localMin.y,
                                  (corner & 4) ? localMax.z : localMin.z);
            const glm::vec3 point = glm::vec3(world * glm::vec4(local, 1.0f));
            low = glm::min(low, point);
            high = glm::max(high, point);
        }
        InvalidateNavigationRegion(*scene, low, high);
    }

    render::Material *TerrainVolumeComponent::ChunkMaterial() const
    {
        if (!m_materialAsset.empty())
            if (auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(m_materialAsset))
                return material;
        const auto *terrain = FindTerrain();
        return terrain ? terrain->GetMaterial() : nullptr;
    }
}
