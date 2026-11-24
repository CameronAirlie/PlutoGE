#include "PlutoGE/scene/components/TerrainVolumeComponent.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/TerrainLayers.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
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

    // The chunk entities draw with ChunkMaterial(); Step reapplies it when it changes.

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
        RefreshChunkMaterial();
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

    void TerrainVolumeComponent::SetMesher(volume::VolumeMesherKind kind)
    {
        if (kind == m_mesherKind)
            return;
        m_mesherKind = kind;
        // Results queued for the old mesher are dropped; every chunk meshes again with the new one.
        m_queue = std::make_unique<volume::VolumeMeshQueue>(volume::MakeVolumeMesher(kind));
        if (m_volume)
        {
            m_volume->SetMeshApron(m_queue->GetMesher().Apron());
            for (const auto &[chunk, id] : m_chunkEntities)
                m_toMesh.insert(chunk);
        }
    }

    void TerrainVolumeComponent::SetShading(const volume::VolumeShadingSettings &shading)
    {
        auto clamped = shading;
        clamped.burialDepth = std::max(clamped.burialDepth, 0.0f);
        clamped.buriedOpenness = std::clamp(clamped.buriedOpenness, 0.0f, 1.0f);
        if (clamped == m_shading)
            return;
        m_shading = clamped;
        for (const auto &[chunk, id] : m_chunkEntities)
            m_toMesh.insert(chunk);
    }

    void TerrainVolumeComponent::SetGroundMaterials(const volume::TerrainGroundMaterials &materials)
    {
        m_ground = materials.Normalised(); // the base follows on the next update
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
            {"Mesher", PropertyType::Enum, std::to_string(static_cast<int>(m_mesherKind)), {"Surface Nets", "Dual Contouring"}},
            {"BurialDepth", PropertyType::Float, std::to_string(m_shading.burialDepth)},
            {"BuriedOpenness", PropertyType::Float, std::to_string(m_shading.buriedOpenness)},
            {"Strata", PropertyType::String, m_ground.Encode()},
        };
    }

    void TerrainVolumeComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto coupling = m_coupling;
        auto ground = m_ground;
        auto shading = m_shading;
        int legacyRockLayer = -1;
        float legacyRockDepth = 1.0f;
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
            else if (property.name == "Mesher")
                SetMesher(static_cast<volume::VolumeMesherKind>(std::clamp(ParseInt(text, static_cast<int>(m_mesherKind)), 0, 1)));
            else if (property.name == "BurialDepth")
                shading.burialDepth = ParseFloat(text, shading.burialDepth);
            else if (property.name == "BuriedOpenness")
                shading.buriedOpenness = ParseFloat(text, shading.buriedOpenness);
            else if (property.name == "Strata")
                ground = volume::TerrainGroundMaterials::Decode(text);
            // Before strata: one rock layer from a depth (read into one stratum).
            else if (property.name == "RockLayer")
                legacyRockLayer = ParseInt(text, -1);
            else if (property.name == "RockDepth")
                legacyRockDepth = ParseFloat(text, legacyRockDepth);
        }
        SetCoupling(coupling);
        if (legacyRockLayer >= 0 && ground.strata.empty())
            ground = volume::TerrainGroundMaterials::Rock(legacyRockLayer, legacyRockDepth);
        SetGroundMaterials(ground);
        SetShading(shading);
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
        // Edits copy the base into the chunks they store: it must be the current one (materials, caves, heights).
        if (auto *terrain = FindTerrain())
            FollowBase(*terrain, true);
        return m_volume->Apply(localShape, edit);
    }

    volume::VolumeMesh TerrainVolumeComponent::MeshRemovedRock(const volume::VolumeShape &worldShape, glm::vec3 &localOrigin)
    {
        const auto *owner = GetOwner();
        if (!owner || !EnsureVolume())
            return {};
        if (auto *terrain = FindTerrain())
            FollowBase(*terrain, true);
        const volume::TransformedShape local(worldShape, owner->GetWorldTransform());
        localOrigin = (local.BoundsMin() + local.BoundsMax()) * 0.5f;
        return volume::MeshRemovedRock(*m_volume, local, m_queue->GetMesher(), localOrigin);
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
        m_savedRevision = m_volume->GetRevision();
        return true;
    }

    std::vector<std::uint8_t> TerrainVolumeComponent::ExportEdits()
    {
        if (!EnsureVolume())
            return {};
        return volume::VolumeFile::Encode(*m_volume);
    }

    bool TerrainVolumeComponent::ImportEdits(std::span<const std::uint8_t> bytes, std::string *error)
    {
        const auto fail = [&](std::string reason) {
            m_lastError = reason;
            if (error)
                *error = std::move(reason);
            return false;
        };
        if (!EnsureVolume())
            return fail("A terrain volume needs a terrain beside it.");
        std::string reason;
        auto contents = volume::VolumeFile::Decode(bytes, &reason);
        if (!contents)
            return fail(reason);
        if (!(contents->grid == m_volume->GetGrid()))
            return fail("The edits were made for another voxel size or chunk size.");
        // Every chunk the old edits or the new ones touch meshes again; holes follow in the next update.
        m_volume->ClearChunks();
        for (auto &[coord, chunk] : contents->chunks)
            m_volume->SetChunk(coord, std::move(chunk));
        m_savedRevision = m_volume->GetRevision();
        return true;
    }

    bool TerrainVolumeComponent::ReloadFile(std::string *error)
    {
        if (!EnsureVolume())
        {
            if (error)
                *error = "A terrain volume needs a terrain beside it.";
            return false;
        }
        const auto bytes = m_volumeFile.empty() ? std::nullopt : ReadFileIfPresent(ResolvePath(m_volumeFile));
        if (bytes)
            return ImportEdits(*bytes, error);
        return ImportEdits(volume::VolumeFile::Encode(m_volume->GetGrid(), {}), error);
    }

    bool TerrainVolumeComponent::HasUnsavedEdits() const
    {
        return m_volume && m_volume->GetRevision() != m_savedRevision;
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
        m_savedRevision = m_volume->GetRevision();
    }

    // ---- the volume ------------------------------------------------------------------------------------------------------

    TerrainComponent *TerrainVolumeComponent::FindTerrain() const
    {
        auto *owner = GetOwner();
        return owner ? owner->GetComponent<TerrainComponent>() : nullptr;
    }

    std::shared_ptr<const volume::TerrainSurface> TerrainVolumeComponent::SnapshotSurface(const TerrainComponent &terrain)
    {
        const int width = terrain.GetWidth(), depth = terrain.GetDepth();
        std::vector<float> heights(static_cast<std::size_t>(width) * depth);
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
                heights[static_cast<std::size_t>(z) * width + x] = terrain.GetSurfaceSampleHeight(x, z);
        // Each sample's strongest painted layer: dug ground shows what the surface around it shows.
        std::vector<volume::VolumeMaterial> layers;
        const auto &stack = terrain.GetLayers();
        if (!stack.Empty() && width > 1 && depth > 1)
        {
            layers.resize(heights.size());
            for (int z = 0; z < depth; ++z)
                for (int x = 0; x < width; ++x)
                    layers[static_cast<std::size_t>(z) * width + x] = static_cast<volume::VolumeMaterial>(std::min<std::size_t>(
                        stack.GetWeights().DominantLayer({static_cast<float>(x) / (width - 1), static_cast<float>(z) / (depth - 1)}), 255));
        }
        return std::make_shared<const volume::TerrainSurface>(width, depth, terrain.GetCellSize(), std::move(heights), std::move(layers));
    }

    TerrainVolumeComponent::BaseInputs TerrainVolumeComponent::CurrentBaseInputs(const TerrainComponent &terrain) const
    {
        BaseInputs inputs;
        inputs.heightsRevision = terrain.GetHeightsRevision();
        inputs.weightsRevision = terrain.GetLayers().GetWeightsRevision();
        inputs.ground = m_ground;
        if (const auto *owner = GetOwner())
        {
            if (const auto *procedural = owner->GetComponent<ProceduralTerrainComponent>(); procedural && procedural->GetDefinition().caves.enabled)
            {
                inputs.caves = procedural->GetDefinition().caves;
                inputs.caveSeed = procedural->GetDefinition().seed;
                // Caves are generated in world space, so a world's tiles agree where they meet.
                inputs.worldOffset = glm::vec3(owner->GetWorldTransform()[3]);
            }
            // The stamps over the terrain, seen from its space.
            if (const auto *scene = owner->GetScene())
            {
                const glm::mat4 terrainWorld = owner->GetWorldTransform();
                const glm::vec2 extent(std::max(1, terrain.GetWidth() - 1) * terrain.GetCellSize(),
                                       std::max(1, terrain.GetDepth() - 1) * terrain.GetCellSize());
                VolumeStampComponent::ForEach(*scene, [&](const VolumeStampComponent &stamp) {
                    StampInput input{stamp.GetShape(), stamp.GetMode(), stamp.GetLayer(), stamp.GetRadius(), stamp.GetHalfExtents(),
                                     stamp.GetLength(), glm::inverse(stamp.GetOwner()->GetWorldTransform()) * terrainWorld};
                    // Its bounds in the terrain's space, against the terrain's footprint.
                    const auto shape = VolumeStampComponent::MakeShape(input.shape, input.radius, input.halfExtents, input.length);
                    const volume::TransformedShape local(*shape, input.toStamp);
                    if (local.BoundsMax().x < 0.0f || local.BoundsMax().z < 0.0f || local.BoundsMin().x > extent.x || local.BoundsMin().z > extent.y)
                        return;
                    inputs.stamps.push_back(input);
                });
            }
        }
        return inputs;
    }

    std::shared_ptr<const volume::VolumeDensitySource> TerrainVolumeComponent::MakeBase(const TerrainComponent &terrain, const BaseInputs &inputs)
    {
        m_surface = SnapshotSurface(terrain);
        return MakeBaseOver(m_surface, inputs);
    }

    std::shared_ptr<const volume::VolumeDensitySource> TerrainVolumeComponent::MakeBaseOver(const std::shared_ptr<const volume::TerrainSurface> &surface,
                                                                                         const BaseInputs &inputs)
    {
        auto ground = volume::MakeTerrainDensity(surface, inputs.ground);
        if (!inputs.caves.enabled && inputs.stamps.empty())
            return ground;
        auto base = std::make_shared<volume::CompositeDensity>();
        base->Add(std::move(ground), volume::CompositeDensity::Operation::Union);
        if (inputs.caves.enabled)
        {
            const glm::vec3 extent(surface->GetCellsX() * surface->GetCellSize(), 0.0f, surface->GetCellsZ() * surface->GetCellSize());
            const glm::vec3 low(0.0f, inputs.caves.altitude.x - inputs.worldOffset.y - 64.0f, 0.0f);
            const glm::vec3 high(extent.x, inputs.caves.altitude.y - inputs.worldOffset.y + 64.0f, extent.z);
            base->Add(std::make_shared<volume::CaveDensity>(inputs.caves, inputs.caveSeed, inputs.worldOffset, glm::min(low, high),
                                                            glm::max(low, high)),
                      volume::CompositeDensity::Operation::Subtract);
        }
        // Authored stamps last: they can plug a generated cave or carve one where generation made none.
        for (const auto &stamp : inputs.stamps)
        {
            auto shape = std::make_shared<volume::TransformedShape>(
                VolumeStampComponent::MakeShape(stamp.shape, stamp.radius, stamp.halfExtents, stamp.length), stamp.toStamp);
            const auto material = static_cast<volume::VolumeMaterial>(std::clamp(stamp.layer, 0, 255));
            switch (stamp.mode)
            {
            case volume::VolumeEditMode::Subtract:
                base->Add(std::make_shared<volume::ShapeDensity>(std::move(shape)), volume::CompositeDensity::Operation::Subtract);
                break;
            case volume::VolumeEditMode::Add:
                base->Add(std::make_shared<volume::ShapeDensity>(std::move(shape), material), volume::CompositeDensity::Operation::Union);
                break;
            case volume::VolumeEditMode::Paint:
                base->Add(std::make_shared<volume::ShapeDensity>(std::move(shape), material), volume::CompositeDensity::Operation::Paint);
                break;
            }
        }
        return base;
    }

    volume::VolumeFarProxy TerrainVolumeComponent::BuildFarProxy(std::shared_ptr<const volume::TerrainSurface> farSurface, int step)
    {
        const auto job = PrepareFarProxy(std::move(farSurface), step);
        return job ? job->Build() : volume::VolumeFarProxy{};
    }

    std::optional<TerrainVolumeComponent::FarProxyJob> TerrainVolumeComponent::PrepareFarProxy(std::shared_ptr<const volume::TerrainSurface> farSurface,
                                                                                             int step)
    {
        if (!farSurface || !farSurface->Valid() || !EnsureVolume())
            return std::nullopt;
        if (auto *terrain = FindTerrain())
            FollowBase(*terrain, true);
        // Plain ground draws itself far away: only edits and features need a proxy.
        if (m_volume->GetStoredChunkCount() == 0 && m_baseInputs.stamps.empty() && !m_baseInputs.caves.enabled)
            return std::nullopt;
        volume::VolumeFarProxySettings settings;
        settings.step = step;
        settings.coupling = m_coupling;
        settings.shading = m_shading;
        auto base = MakeBaseOver(farSurface, m_baseInputs);
        return FarProxyJob{*m_volume, std::move(base), std::move(farSurface), m_queue->GetSharedMesher(), settings};
    }

    volume::VolumeFarProxy TerrainVolumeComponent::FarProxyJob::Build() const
    {
        return volume::BuildVolumeFarProxy(volume, base, *surface, *mesher, settings);
    }

    void TerrainVolumeComponent::QueueBaseFeatures()
    {
        for (const auto chunk : m_volume->GetBaseFeatureChunks())
            m_toMesh.insert(chunk);
    }

    bool TerrainVolumeComponent::EnsureVolume()
    {
        if (m_volume)
            return true;
        auto *terrain = FindTerrain();
        if (!terrain)
            return false;
        m_baseInputs = CurrentBaseInputs(*terrain);
        m_weightsSeen = m_baseInputs.weightsRevision;
        volume::VolumeGrid grid;
        grid.voxelSize = m_voxelSize;
        grid.chunkVoxels = m_chunkVoxels;
        m_volume = std::make_unique<volume::TerrainVolume>(grid, MakeBase(*terrain, m_baseInputs));
        m_volume->SetMeshApron(m_queue->GetMesher().Apron());
        QueueBaseFeatures();
        m_holes = std::make_shared<const TerrainHoleMask>();
        m_pendingHoles.reset();
        m_holesVolumeRevision = kHolesStale;
        m_savedRevision = m_volume->GetRevision();
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
        QueueBaseFeatures();
    }

    bool TerrainVolumeComponent::IsUpToDate() const
    {
        if (!m_volume)
            return true;
        const auto *terrain = FindTerrain();
        return m_toMesh.empty() && m_queue->Idle() && !m_pendingHoles && m_volume->GetDirtyChunks().empty() &&
               m_holesVolumeRevision == m_volume->GetRevision() && (!terrain || CurrentBaseInputs(*terrain) == m_baseInputs);
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
        { core::CpuScope scope("TerrainVolume.Validate"); ValidateChunkEntities(); }
        { core::CpuScope scope("TerrainVolume.ChunkMaterial"); RefreshChunkMaterial(); }
        { core::CpuScope scope("TerrainVolume.FollowBase"); FollowBase(*terrain, unbudgeted); }
        for (const auto chunk : m_volume->TakeDirtyChunks())
            m_toMesh.insert(chunk);
        { core::CpuScope scope("TerrainVolume.RefreshHoles"); RefreshHoles(); }
        const std::size_t budget = unbudgeted ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(m_meshesPerFrame);
        { core::CpuScope scope("TerrainVolume.ScheduleMeshing"); ScheduleMeshing(budget); }
        { core::CpuScope scope("TerrainVolume.ApplyMeshes"); ApplyMeshes(m_queue->Collect(budget, unbudgeted)); }
        { core::CpuScope scope("TerrainVolume.ApplyHoles"); ApplyHolesWhenMeshed(*terrain); }
    }

    void TerrainVolumeComponent::FollowBase(TerrainComponent &terrain, bool immediately)
    {
        core::CpuScope inputsScope("TerrainVolume.CurrentBaseInputs");
        auto inputs = CurrentBaseInputs(terrain);
        inputsScope.End();
        if (inputs == m_baseInputs)
            return;
        core::CpuScope rebaseScope("TerrainVolume.Rebase");
        // Painting changes the weights every dab: wait until a frame passes without one (Flush does not wait).
        const bool painting = inputs.weightsRevision != m_weightsSeen;
        m_weightsSeen = inputs.weightsRevision;
        if (painting && !immediately)
        {
            auto withoutWeights = inputs;
            withoutWeights.weightsRevision = m_baseInputs.weightsRevision;
            if (withoutWeights == m_baseInputs)
                return;
        }
        // Sculpted, painted, regenerated or moved: unstored chunks follow the new base; every mesh (the old caves' and
        // the new ones') and the holes are made again.
        for (const auto chunk : m_volume->GetBaseFeatureChunks())
            m_toMesh.insert(chunk);
        m_baseInputs = inputs;
        m_volume->SetBase(MakeBase(terrain, m_baseInputs));
        for (const auto &[chunk, id] : m_chunkEntities)
            m_toMesh.insert(chunk);
        QueueBaseFeatures();
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
        auto finish = [surface = m_surface, holes = m_pendingHoles ? m_pendingHoles : m_holes, coupling = m_coupling, shading = m_shading,
                       grid = m_volume->GetGrid()](volume::VolumeChunkCoord chunk, volume::VolumeMesh &mesh) {
            volume::FitVolumeMeshToTerrain(mesh, grid.ChunkOrigin(chunk), *surface, *holes, coupling, grid.voxelSize);
            volume::ApplyBurialOcclusion(mesh, grid.ChunkOrigin(chunk), *surface, shading);
            volume::EncodeTriangleMaterials(mesh);
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
                meshComponent->SetMaterial(m_appliedMaterial);
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

    render::Material *TerrainVolumeComponent::ChunkMaterial()
    {
        if (!m_materialAsset.empty())
            if (auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(m_materialAsset))
                return material;
        if (auto *layered = LayeredTerrainMaterial())
            return layered;
        const auto *terrain = FindTerrain();
        return terrain ? terrain->GetMaterial() : nullptr;
    }

    render::Material *TerrainVolumeComponent::LayeredTerrainMaterial()
    {
        auto *terrain = FindTerrain();
        auto *source = terrain ? terrain->GetRenderMaterial() : nullptr;
        if (!source || !source->ReadConfig().terrainLayers)
        {
            // Kept (not freed): a chunk may still draw with it until RefreshChunkMaterial swaps it out.
            m_layeredSource = nullptr;
            return nullptr;
        }
        // Rebuilt when the terrain's layered material changes (its layers, their maps, the material under them);
        // a revision of 0 means untracked edits, which the terrain's own copy does not follow either.
        const auto revision = source->GetRevision();
        if (m_layeredMaterial && m_layeredSource == source && (revision == 0 || revision == m_layeredSourceRevision))
            return m_layeredMaterial.get();
        auto config = source->ReadConfig();
        auto layers = std::make_shared<render::TerrainLayerSet>(*config.terrainLayers);
        // Chunks have no terrain UVs: each triangle names its layer (EncodeTriangleMaterials), projected three ways
        // so cave walls and overhangs do not smear. The macro albedo is mapped by terrain UVs too.
        layers->weights = render::TerrainLayerWeights::VertexLayer;
        layers->projection = render::TerrainLayerProjection::Triplanar;
        layers->weightMaps = {};
        config.terrainLayers = std::move(layers);
        config.albedoTexture = nullptr;
        if (!m_layeredMaterial)
            m_layeredMaterial = std::make_unique<render::Material>(config);
        else
            m_layeredMaterial->SetConfig(std::move(config));
        m_layeredSource = source;
        m_layeredSourceRevision = revision;
        return m_layeredMaterial.get();
    }

    void TerrainVolumeComponent::RefreshChunkMaterial()
    {
        auto *material = ChunkMaterial();
        if (material == m_appliedMaterial)
            return;
        m_appliedMaterial = material;
        auto *scene = GetOwner() ? GetOwner()->GetScene() : nullptr;
        if (!scene)
            return;
        for (const auto &[chunk, id] : m_chunkEntities)
            if (auto *entity = scene->FindEntityByID(id))
                if (auto *mesh = entity->GetComponent<MeshComponent>())
                    mesh->SetMaterial(material);
    }
}
