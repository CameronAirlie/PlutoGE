// TerrainVolumeComponent in a scene (docs/dev/TERRAIN_VOLUMES_PLAN.md, phase 3): world-space edits, chunk entities with
// mesh colliders, derived holes kept apart from authored ones, collision through a dug pit, enabling and removing,
// the volume file, and world tiles that keep their own volume file.
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/TerrainVolumeEditing.h"
#include "PlutoGE/scene/TerrainWorldGrid.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/VolumeStampComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/TerrainLayers.h"
#include "PlutoGE/scene/volume/VolumeEditArchive.h"
#include "PlutoGE/scene/volume/VolumeFile.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    std::string FlatSamples(int width, int depth, float height)
    {
        std::string text;
        for (int i = 0; i < width * depth; ++i)
            text += (i ? "," : "") + std::to_string(height);
        return text;
    }

    std::string PropertyText(const std::vector<Property> &properties, const std::string &name)
    {
        for (const auto &property : properties)
            if (property.name == name)
                return property.Text();
        return {};
    }

    std::filesystem::path TempDirectory()
    {
        const auto path = std::filesystem::temp_directory_path() / "PlutoGETerrainVolumeComponentTests";
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
        return path;
    }

    // A flat terrain of 33 x 33 samples, 1 m apart, 5 m high, at (100, 0, 50), with a collider and a volume.
    struct Ground
    {
        Scene scene;
        Entity *entity = nullptr;
        TerrainComponent *terrain = nullptr;
        TerrainVolumeComponent *volume = nullptr;

        Ground()
        {
            entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
            entity->SetPosition({100.0f, 0.0f, 50.0f});
            entity->AddTag("ground");
            terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 33, .depth = 33, .cellSize = 1.0f});
            terrain->Deserialize({{"HeightSamples", PropertyType::String, FlatSamples(33, 33, 5.0f)}});
            terrain->SetStatic(true);
            entity->CreateComponent<ColliderComponent>()->SetShape(ColliderShape::Terrain);
            volume = entity->CreateComponent<TerrainVolumeComponent>();
            volume->SetChunkVoxels(8);
        }
    };

    std::size_t ChildChunks(const Entity &terrain)
    {
        return static_cast<std::size_t>(std::count_if(terrain.GetChildren().begin(), terrain.GetChildren().end(), [](const Entity *child) {
            const auto &tags = child->GetTags();
            return std::find(tags.begin(), tags.end(), std::string(TerrainVolumeComponent::ChunkTag)) != tags.end();
        }));
    }

    void DigAPit()
    {
        Ground ground;
        // The pit's centre in world space: terrain cell (16, 16).
        const glm::vec3 centre(116.0f, 5.0f, 66.0f);
        PhysicsRaycastHit hit;
        Require(ground.scene.Raycast({116.0f, 20.0f, 66.0f}, {0, -1, 0}, 50.0f, hit) && std::abs(hit.point.y - 5.0f) < 0.01f,
                "The ground collides before digging");

        // An authored hole elsewhere: the volume's holes join it without touching it.
        ground.terrain->SetHole(2, 2, true);
        const auto result = ground.volume->Apply(volume::SphereShape(centre, 2.0f), {volume::VolumeEditMode::Subtract});
        Require(result.Changed(), "Digging the terrain changes its volume");
        // Nothing changes on the terrain until the meshes are in.
        Require(!ground.volume->IsUpToDate() && !ground.terrain->GetEffectiveHoles().IsHole(16, 16), "Holes wait for their meshes");
        ground.volume->Flush();
        Require(ground.volume->IsUpToDate(), "Flushing lands every mesh and hole");

        // Derived holes: the terrain draws and collides around them, but its authored holes (and its saved HoleMask) are untouched.
        const auto *derived = ground.terrain->FindDerivedHoles(TerrainVolumeComponent::HoleSource);
        Require(derived && derived->IsHole(16, 16) && ground.terrain->GetEffectiveHoles().IsHole(16, 16), "The pit's cells are cut");
        Require(!ground.terrain->GetEffectiveHoles().IsHole(4, 4), "Cells away from the pit are not");
        Require(ground.terrain->GetEffectiveHoles().IsHole(2, 2), "Authored and derived holes both cut the terrain");
        Require(ground.terrain->GetHoles().GetHoleCount() == 1 && ground.terrain->GetHoles().IsHole(2, 2) &&
                    PropertyText(ground.terrain->Serialize(), "HoleMask") == ground.terrain->GetHoles().Encode(),
                "Derived holes are never authored or saved");
        Require(ground.terrain->IsHoleAtWorldPosition(centre), "Terrain queries see the derived holes");

        // Chunk entities: transient children carrying the terrain's tags, a mesh and a mesh collider.
        Require(ground.volume->GetChunkEntityCount() > 0 && ChildChunks(*ground.entity) == ground.volume->GetChunkEntityCount(),
                "The pit's chunks became child entities");
        const auto chunk = ground.volume->GetVolume()->GetGrid().ChunkAt({16.0f, 3.0f, 16.0f});
        auto *chunkEntity = ground.volume->GetChunkEntity(chunk);
        Require(chunkEntity && chunkEntity->IsTransient() && chunkEntity->GetParent() == ground.entity, "Chunks are transient children");
        const auto &tags = chunkEntity->GetTags();
        Require(std::find(tags.begin(), tags.end(), "ground") != tags.end(), "Chunks carry the terrain's tags");
        auto *mesh = chunkEntity->GetComponent<MeshComponent>();
        auto *collider = chunkEntity->GetComponent<ColliderComponent>();
        Require(mesh && mesh->HasRuntimeGeometry() && collider && collider->GetShape() == ColliderShape::Mesh, "Chunks draw and collide");

        // A ray down the middle falls through the cut terrain onto the pit's floor, 2 m down.
        Require(ground.scene.Raycast({116.0f, 20.0f, 66.0f}, {0, -1, 0}, 50.0f, hit), "The pit's floor collides");
        Require(std::abs(hit.point.y - 3.0f) < 0.15f && hit.entityId != ground.entity->GetID(),
                "The ray stops on the dug floor, not the old ground (hit at " + std::to_string(hit.point.y) + ")");
        Require(ground.scene.Raycast({106.0f, 20.0f, 56.0f}, {0, -1, 0}, 50.0f, hit) && std::abs(hit.point.y - 5.0f) < 0.01f &&
                    hit.entityId == ground.entity->GetID(),
                "The terrain still has the ground away from the pit");

        // Disabled, the terrain has its ground back; enabled, the pit returns.
        ground.volume->SetEnabled(false);
        Require(ground.terrain->GetEffectiveHoles() == ground.terrain->GetHoles() && ground.volume->GetChunkEntityCount() == 0,
                "Disabling restores the terrain");
        ground.volume->SetEnabled(true);
        ground.volume->Flush();
        Require(ground.terrain->GetEffectiveHoles().IsHole(16, 16) && ground.volume->GetChunkEntityCount() > 0, "Enabling digs it again");

        // A chunk entity removed behind the component's back (undo of a scene edit) comes back.
        ground.scene.DestroyEntity(chunkEntity->GetID());
        ground.scene.Update(0.0f);
        ground.volume->Flush();
        Require(ground.volume->GetChunkEntity(chunk), "A lost chunk entity is made again");

        // Removing the component leaves the terrain as it was.
        ground.entity->RemoveComponent(ground.volume);
        ground.scene.Update(0.0f);
        Require(ground.terrain->GetEffectiveHoles() == ground.terrain->GetHoles() && !ground.terrain->FindDerivedHoles(TerrainVolumeComponent::HoleSource) &&
                    ChildChunks(*ground.entity) == 0,
                "Removing the volume removes its ground");
    }

    void CaveUnderground()
    {
        Ground ground;
        // A tunnel 3 m under the surface: drawn and collided with, but the surface stays whole.
        ground.volume->Apply(volume::CapsuleShape({104.0f, 1.5f, 60.0f}, {120.0f, 1.5f, 60.0f}, 1.0f), {volume::VolumeEditMode::Subtract});
        ground.volume->Flush();
        Require(ground.terrain->GetEffectiveHoles().Empty(), "A cave below the surface cuts no holes");
        Require(ground.volume->GetChunkEntityCount() > 0, "The cave's walls are meshed");
        PhysicsRaycastHit hit;
        Require(ground.scene.Raycast({112.0f, 1.5f, 60.0f}, {0, -1, 0}, 10.0f, hit) && std::abs(hit.point.y - 0.5f) < 0.15f,
                "Inside the cave, the floor collides (hit at " + std::to_string(hit.point.y) + ")");
        Require(ground.scene.Raycast({112.0f, 20.0f, 60.0f}, {0, -1, 0}, 50.0f, hit) && hit.entityId == ground.entity->GetID() &&
                    std::abs(hit.point.y - 5.0f) < 0.01f,
                "From above, the ground over the cave is the terrain's");
    }

    void Files()
    {
        const auto directory = TempDirectory();
        const auto file = (directory / "pit.plutovolume").string();
        std::size_t chunks = 0;
        {
            Ground ground;
            std::string error;
            Require(!ground.volume->SaveFile(&error) && !error.empty(), "Saving needs a file name");
            ground.volume->SetVolumeFile(file);
            ground.volume->Apply(volume::SphereShape({116.0f, 5.0f, 66.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
            ground.volume->Flush();
            chunks = ground.volume->GetChunkEntityCount();
            Require(ground.volume->SaveFile(&error) && std::filesystem::exists(file), "Saving writes the stored chunks: " + error);
        }
        {
            Ground ground;
            ground.volume->SetVolumeFile(file);
            ground.volume->Flush();
            Require(ground.volume->GetVolume()->GetStoredChunkCount() > 0 && ground.volume->GetChunkEntityCount() == chunks &&
                        ground.terrain->GetEffectiveHoles().IsHole(16, 16),
                    "A volume loads its file and digs the same pit");
            // A volume left with no stored chunks removes its file.
            for (const auto coord : ground.volume->GetVolume()->GetStoredChunks())
                ground.volume->GetVolume()->RemoveChunk(coord);
            Require(ground.volume->SaveFile() && !std::filesystem::exists(file), "An empty volume has no file");
        }
        {
            Ground ground;
            ground.volume->ProvideFileContents(std::vector<std::uint8_t>{'b', 'a', 'd'});
            ground.volume->Flush();
            Require(!ground.volume->GetLastError().empty() && ground.volume->GetVolume()->GetStoredChunkCount() == 0,
                    "A damaged file is reported and ignored");
        }
    }

    void Settings()
    {
        Ground ground;
        ground.volume->SetVoxelSize(0.25f);
        ground.volume->SetCollision(false);
        auto coupling = ground.volume->GetCoupling();
        coupling.overlapCells = 2;
        ground.volume->SetCoupling(coupling);
        const auto properties = ground.volume->Serialize();
        TerrainVolumeComponent copy;
        copy.Deserialize(properties);
        Require(copy.GetVoxelSize() == 0.25f && copy.GetChunkVoxels() == 8 && !copy.GetCollision() && copy.GetCoupling().overlapCells == 2,
                "Settings round-trip");
        ground.volume->Apply(volume::SphereShape({116.0f, 5.0f, 66.0f}, 1.0f), {volume::VolumeEditMode::Subtract});
        ground.volume->Flush();
        auto *chunk = ground.volume->GetChunkEntity(ground.volume->GetVolume()->GetGrid().ChunkAt({16.0f, 4.5f, 16.0f}));
        Require(chunk && !chunk->GetComponent<ColliderComponent>(), "Without collision, chunks only draw");
        ground.volume->SetCollision(true);
        Require(chunk->GetComponent<ColliderComponent>(), "Turning collision on gives the chunks colliders");
        // A new lattice starts again (unsaved edits go).
        ground.volume->SetVoxelSize(0.5f);
        Require(ground.volume->GetChunkEntityCount() == 0 && ground.terrain->GetEffectiveHoles().Empty() &&
                    ground.volume->GetVolume()->GetStoredChunkCount() == 0,
                "Changing the voxel size starts the volume again");
    }

    void WorldTiles()
    {
        const auto directory = TempDirectory();
        TerrainWorldGrid grid;
        grid.tilesX = 2;
        grid.tilesZ = 1;
        grid.tileSamples = 17;
        grid.cellSize = 2.0f;
        grid.heightScale = 50.0f;
        TerrainHeightField field{grid.WorldSamplesX(), grid.WorldSamplesZ(), {}};
        field.samples.assign(static_cast<std::size_t>(field.width) * field.depth, 10.0f);
        const std::string pattern = (directory / "tile_{x}_{z}.plutoheight").string();
        Require(TerrainWorldComponent::WriteWorldFiles(field, grid, pattern, {}, 4).Ok(), "Writing the world");

        const auto makeWorld = [&](Scene &scene) {
            auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "World"}));
            auto *world = entity->CreateComponent<TerrainWorldComponent>();
            world->SetGrid(grid);
            world->SetTileFiles(pattern);
            TerrainStreamingSettings streaming;
            streaming.loadRadius = 5.0f;
            streaming.unloadRadius = 10.0f;
            world->SetStreaming(streaming);
            auto *tileTemplate = world->EnsureTileTemplate();
            tileTemplate->CreateComponent<TerrainVolumeComponent>()->SetChunkVoxels(8);
            return world;
        };

        {
            Scene scene;
            auto *world = makeWorld(scene);
            Require(world->LoadAround({8.0f, 0.0f, 8.0f}, std::chrono::seconds(10)), "Loading tile (0,0)");
            auto *tile = world->GetTileEntity({0, 0});
            auto *volume = tile ? tile->GetComponent<TerrainVolumeComponent>() : nullptr;
            Require(volume && volume->GetVolumeFile() == world->TileVolumeFile({0, 0}), "Tiles copy the template's volume, with their own file");
            Require(world->TileVolumeFile({0, 0}) == (directory / "tile_0_0_volume.plutovolume").string(), "The volume file sits beside the heights");
            volume->Apply(volume::SphereShape({8.0f, 10.0f, 8.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
            volume->Flush();
            Require(volume->GetChunkEntityCount() > 0 && world->GetTileTerrain({0, 0})->GetEffectiveHoles().IsHole(4, 4), "A tile can be dug");
            Require(volume->SaveFile() && std::filesystem::exists(directory / "tile_0_0_volume.plutovolume"), "A tile saves its volume file");
        }
        {
            Scene scene;
            auto *world = makeWorld(scene);
            Require(world->LoadAround({8.0f, 0.0f, 8.0f}, std::chrono::seconds(10)), "Loading tile (0,0) again");
            auto *volume = world->GetTileEntity({0, 0})->GetComponent<TerrainVolumeComponent>();
            volume->Flush();
            Require(volume->GetVolume()->GetStoredChunkCount() > 0 && world->GetTileTerrain({0, 0})->GetEffectiveHoles().IsHole(4, 4) &&
                        volume->GetChunkEntityCount() > 0,
                    "A streamed tile brings its dug ground back");
            Require(world->GetTileTerrain({0, 0})->GetHoles().Empty(), "and leaves its authored holes alone");
        }
    }
}

namespace
{
    // Phase 4: chunks draw with the terrain's layers, re-projected (vertex layers, triplanar, no macro albedo), and
    // follow the terrain's material as it changes.
    void LayeredChunks()
    {
        Ground ground;
        render::Material plain;
        ground.terrain->SetMaterial(&plain);
        ground.volume->Apply(volume::SphereShape({116.0f, 5.0f, 66.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
        ground.volume->Flush();
        const auto chunk = ground.volume->GetVolume()->GetGrid().ChunkAt({16.0f, 3.0f, 16.0f});
        auto *mesh = ground.volume->GetChunkEntity(chunk)->GetComponent<MeshComponent>();
        Require(mesh->GetMaterial() == &plain, "Without layers, chunks draw with the terrain's material");

        TerrainLayer rock;
        rock.name = "Rock";
        rock.tileSize = 2.0f;
        Require(ground.terrain->GetLayers().AddLayer(rock), "The terrain takes a layer");
        ground.volume->Update(0.0f);
        const auto *layered = mesh->GetMaterial();
        Require(layered && layered != &plain && layered != ground.terrain->GetRenderMaterial(), "Layers give chunks their own material");
        const auto &config = layered->ReadConfig();
        Require(config.terrainLayers && config.terrainLayers->layers.size() == 1 &&
                    config.terrainLayers->weights == render::TerrainLayerWeights::VertexLayer &&
                    config.terrainLayers->projection == render::TerrainLayerProjection::Triplanar && !config.albedoTexture,
                "The chunk material names layers per vertex, projects them triplanar and has no macro albedo");
        Require(ground.terrain->GetRenderMaterial()->ReadConfig().terrainLayers->weights == render::TerrainLayerWeights::Maps,
                "The terrain's own material keeps its weight maps");
        const auto &vertex = mesh->GetMesh()->GetMeshData().vertices.front();
        Require(vertex.uv[0] == 0.0f, "Chunk vertices name their layer");

        // Another layer reaches the chunks without remeshing them.
        TerrainLayer sand;
        sand.name = "Sand";
        ground.terrain->GetLayers().AddLayer(sand);
        ground.volume->Update(0.0f);
        Require(mesh->GetMaterial() == layered && layered->ReadConfig().terrainLayers->layers.size() == 2,
                "The chunk material follows the terrain's layers");
    }
}

namespace
{
    // Phase 6: a ProceduralTerrainComponent's caves are carved without edits, open mouths, follow the definition, and
    // dug ground below the rock depth takes the rock layer.
    void ProceduralCaves()
    {
        Ground ground;
        auto *procedural = ground.entity->CreateComponent<ProceduralTerrainComponent>();
        procedural::ProceduralTerrainDefinition definition;
        definition.seed = 3;
        definition.caves.enabled = true;
        definition.caves.cellSize = 32.0f;
        definition.caves.tunnelsPerCell = 2;
        definition.caves.tunnelLength = 40.0f;
        definition.caves.radius = {1.5f, 2.0f};
        definition.caves.altitude = {2.0f, 6.0f}; // the ground is at 5 m: tunnels open to the surface
        definition.caves.chamberChance = 0.0f;
        procedural->SetDefinition(definition);
        // The caves travel with the definition (also checked by PlutoGEProceduralTerrainTests).
        const auto decoded = procedural::ProceduralTerrainDefinition::Decode(definition.Encode());
        Require(decoded && decoded->caves == definition.caves, "The definition keeps its caves");

        ground.volume->Flush();
        Require(ground.volume->GetVolume()->GetStoredChunkCount() == 0 && ground.volume->GetChunkEntityCount() > 0,
                "Caves are meshed without storing anything");
        const auto *derived = ground.terrain->FindDerivedHoles(TerrainVolumeComponent::HoleSource);
        Require(derived && derived->GetHoleCount() > 0, "Cave mouths cut the terrain");
        Require(ground.volume->ExportEdits().size() == volume::VolumeFile::Encode(ground.volume->GetVolume()->GetGrid(), {}).size(),
                "Generated caves are not edits: nothing to save");

        // Deeper caves: the mouths close, the caves stay.
        definition.caves.altitude = {-12.0f, -4.0f};
        procedural->SetDefinition(definition);
        ground.volume->Flush();
        Require(ground.terrain->GetEffectiveHoles().GetHoleCount() == 0 && ground.volume->GetChunkEntityCount() > 0,
                "Deep caves cut nothing but are still there");

        // No caves: the ground is whole again.
        definition.caves.enabled = false;
        procedural->SetDefinition(definition);
        ground.volume->Flush();
        Require(ground.volume->GetChunkEntityCount() == 0, "Without caves nothing is meshed");

        // Rock below a metre: a pit's walls are the rock layer, its rim the surface's.
        ground.volume->SetGroundMaterials({.rockLayer = 2, .rockDepth = 1.0f});
        ground.volume->Apply(volume::SphereShape({116.0f, 5.0f, 66.0f}, 2.5f), {volume::VolumeEditMode::Subtract});
        ground.volume->Flush();
        bool rock = false, surface = false;
        for (const auto *child : ground.entity->GetChildren())
            if (const auto *mesh = child->GetComponent<MeshComponent>(); mesh && mesh->GetMesh())
                for (const auto &vertex : mesh->GetMesh()->GetMeshData().vertices)
                {
                    const auto layers = volume::VertexLayerEncoding::UnpackLayers(vertex.uv[0]);
                    const auto own = vertex.uv[1] >= 1.0f ? layers[0] : vertex.uv2[0] >= 1.0f ? layers[1] : layers[2];
                    rock = rock || own == 2;
                    surface = surface || own == 0;
                }
        Require(rock && surface, "Dug ground shows the rock layer below its depth and the surface's above");
        Require(PropertyText(ground.volume->Serialize(), "RockLayer") == "2", "The rock layer is saved");

        // Phase 8: the mesher and cave shading are settings; changing them meshes the chunks again.
        const auto chunks = ground.volume->GetChunkEntityCount();
        ground.volume->Deserialize({{"Mesher", PropertyType::Enum, "1"}, {"BurialDepth", PropertyType::Float, "3"}});
        Require(ground.volume->GetMesher() == volume::VolumeMesherKind::DualContouring && ground.volume->GetShading().burialDepth == 3.0f &&
                    !ground.volume->IsUpToDate(),
                "Mesher and shading settings remesh");
        ground.volume->Flush();
        Require(ground.volume->IsUpToDate() && ground.volume->GetChunkEntityCount() == chunks, "Dual contouring meshes the same chunks");
    }

    // Phase 7: stamps are part of the base: they carve and fill without storing anything, follow their entity, apply in
    // order, and keep to the terrains under them.
    void Stamps()
    {
        Ground ground;
        auto *stampEntity = ground.scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Cave Stamp"}));
        stampEntity->SetPosition({116.0f, 1.0f, 66.0f}); // a cave 4 m under the ground
        auto *stamp = stampEntity->CreateComponent<VolumeStampComponent>();
        stamp->SetRadius(2.0f);
        ground.volume->Flush();
        Require(ground.volume->GetVolume()->GetStoredChunkCount() == 0 && ground.volume->GetChunkEntityCount() > 0 &&
                    ground.terrain->GetEffectiveHoles().Empty(),
                "A stamp under the ground carves a cave without storing or cutting anything");

        // Raised to the surface it opens; the ground follows the entity.
        stampEntity->SetPosition({116.0f, 5.0f, 66.0f});
        ground.volume->Flush();
        Require(ground.terrain->GetEffectiveHoles().IsHole(16, 16), "A stamp at the surface opens a pit");
        PhysicsRaycastHit hit;
        Require(ground.scene.Raycast({116.0f, 20.0f, 66.0f}, {0, -1, 0}, 50.0f, hit) && std::abs(hit.point.y - 3.0f) < 0.3f,
                "The pit's floor collides 2 m down");

        // A later fill stamp plugs it; order decides.
        auto *plugEntity = ground.scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Plug"}));
        plugEntity->SetPosition({116.0f, 5.0f, 66.0f});
        auto *plug = plugEntity->CreateComponent<VolumeStampComponent>();
        plug->SetShape(VolumeStampComponent::Shape::Box);
        plug->SetHalfExtents({3.0f, 0.75f, 3.0f});
        plug->SetMode(volume::VolumeEditMode::Add);
        plug->SetLayer(1);
        plug->SetOrder(1);
        ground.volume->Flush();
        Require(ground.scene.Raycast({116.0f, 20.0f, 66.0f}, {0, -1, 0}, 50.0f, hit) && std::abs(hit.point.y - 5.75f) < 0.3f,
                "A fill stamp after the carve builds rock over it");
        plug->SetOrder(-1);
        ground.volume->Flush();
        Require(ground.scene.Raycast({116.0f, 20.0f, 66.0f}, {0, -1, 0}, 50.0f, hit) && hit.point.y < 4.0f, "Ordered first, the carve wins");

        // Disabled or far away, stamps leave the ground alone.
        plug->SetEnabled(false);
        stamp->SetEnabled(false);
        ground.volume->Flush();
        Require(ground.volume->GetChunkEntityCount() == 0 && ground.terrain->GetEffectiveHoles().Empty(), "Disabled stamps do nothing");
        stamp->SetEnabled(true);
        stampEntity->SetPosition({300.0f, 5.0f, 66.0f});
        ground.volume->Flush();
        Require(ground.volume->GetChunkEntityCount() == 0, "A stamp off the terrain does nothing to it");
        Require(ground.volume->ExportEdits().size() == volume::VolumeFile::Encode(ground.volume->GetVolume()->GetGrid(), {}).size(),
                "Stamps are never edits");

        // Saved with the scene like any component.
        VolumeStampComponent copy;
        copy.Deserialize(plug->Serialize());
        Require(copy.GetShape() == VolumeStampComponent::Shape::Box && copy.GetMode() == volume::VolumeEditMode::Add && copy.GetLayer() == 1 &&
                    copy.GetHalfExtents() == glm::vec3(3.0f, 0.75f, 3.0f) && copy.GetOrder() == -1,
                "Stamp settings round-trip");
    }

    // Phase 9: a dug tile that streams out leaves its pit in the world's overview.
    void FarTiles()
    {
        const auto directory = TempDirectory();
        TerrainWorldGrid grid;
        grid.tilesX = 2;
        grid.tilesZ = 1;
        grid.tileSamples = 33;
        grid.cellSize = 1.0f;
        grid.heightScale = 50.0f;
        TerrainHeightField field{grid.WorldSamplesX(), grid.WorldSamplesZ(), {}};
        field.samples.assign(static_cast<std::size_t>(field.width) * field.depth, 10.0f);
        const std::string pattern = (directory / "tile_{x}_{z}.plutoheight").string();
        const std::string overview = (directory / "overview.plutoheight").string();
        Require(TerrainWorldComponent::WriteWorldFiles(field, grid, pattern, overview, 4).Ok(), "Writing the world with an overview");

        Scene scene;
        auto *worldEntity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "World"}));
        auto *world = worldEntity->CreateComponent<TerrainWorldComponent>();
        world->SetGrid(grid);
        world->SetTileFiles(pattern);
        world->SetOverviewFile(overview);
        TerrainStreamingSettings streaming;
        streaming.loadRadius = 5.0f;
        streaming.unloadRadius = 10.0f;
        world->SetStreaming(streaming);
        world->EnsureTileTemplate()->CreateComponent<TerrainVolumeComponent>()->SetChunkVoxels(8);
        Require(world->LoadAround({16.0f, 0.0f, 16.0f}, std::chrono::seconds(10)) && world->GetOverviewEntity({0, 0}), "Loading with an overview");
        EditTerrainVolumes(scene, volume::SphereShape({16.0f, 10.0f, 16.0f}, 3.0f), {volume::VolumeEditMode::Subtract});
        Require(!world->HasFarProxy({0, 0}), "A loaded tile draws itself");

        world->UnloadAll();
        Require(world->HasFarProxy({0, 0}) && !world->HasFarProxy({1, 0}), "The dug tile left a far proxy; the other has none");
        auto *overviewEntity = world->GetOverviewEntity({0, 0});
        const auto *holes = overviewEntity->GetComponent<TerrainComponent>()->FindDerivedHoles(TerrainVolumeComponent::HoleSource);
        Require(holes && holes->GetHoleCount() > 0, "The overview is cut where the pit is");
        const auto farMeshes = std::count_if(overviewEntity->GetChildren().begin(), overviewEntity->GetChildren().end(), [](const Entity *child) {
            return child->GetComponent<MeshComponent>() && child->GetComponent<MeshComponent>()->HasRuntimeGeometry();
        });
        Require(farMeshes > 0, "Coarse meshes draw the pit far away");
        world->SetFarVolumeVoxel(0.0f);
        Require(PropertyText(world->Serialize(), "FarVolumeVoxel").starts_with("0"), "The far voxel is a setting");
    }

    // Phase 5: export, import and reload; unsaved edits.
    void EditsInAndOut()
    {
        Ground ground;
        Require(!ground.volume->HasUnsavedEdits(), "A fresh volume has nothing unsaved");
        ground.volume->Apply(volume::SphereShape({116.0f, 5.0f, 66.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
        Require(ground.volume->HasUnsavedEdits(), "Digging leaves unsaved edits");
        const auto dug = ground.volume->ExportEdits();
        const auto storedDug = ground.volume->GetVolume()->GetStoredChunks();

        ground.volume->Apply(volume::SphereShape({104.0f, 5.0f, 54.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
        ground.volume->Flush();
        Require(ground.terrain->GetEffectiveHoles().IsHole(4, 4), "The second pit is cut");

        Require(ground.volume->ImportEdits(dug) && !ground.volume->HasUnsavedEdits(), "Importing replaces the edits");
        Require(ground.volume->GetVolume()->GetStoredChunks() == storedDug, "Only the imported chunks are stored");
        ground.volume->Flush();
        Require(!ground.terrain->GetEffectiveHoles().IsHole(4, 4) && ground.terrain->GetEffectiveHoles().IsHole(16, 16),
                "The second pit closes, the first stays");

        std::string error;
        auto damaged = dug;
        damaged.resize(damaged.size() / 2);
        Require(!ground.volume->ImportEdits(damaged, &error) && !error.empty() && ground.volume->GetVolume()->GetStoredChunks() == storedDug,
                "Damaged edits are refused and change nothing");
        auto otherGrid = ground.volume->GetVolume()->GetGrid();
        otherGrid.voxelSize = 0.25f;
        Require(!ground.volume->ImportEdits(volume::VolumeFile::Encode(otherGrid, {}), &error), "Edits for another grid are refused");

        Require(ground.volume->ReloadFile() && ground.volume->GetVolume()->GetStoredChunkCount() == 0, "Without a file, reloading clears");
        ground.volume->Flush();
        Require(!ground.terrain->GetEffectiveHoles().IsHole(16, 16), "and the ground comes back");
        Require(ground.volume->GetChunkEntityCount() == 0, "and its chunks go: " + std::to_string(ground.volume->GetChunkEntityCount()));
    }

    // Phase 5: one world-space call digs every terrain under it, keeps to a budget and clears foliage.
    void SceneEdits()
    {
        Ground ground;
        // A second terrain east of the first, sharing its edge at x = 132.
        auto *east = ground.scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "East"}));
        east->SetPosition({132.0f, 0.0f, 50.0f});
        auto *eastTerrain = east->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 33, .depth = 33, .cellSize = 1.0f});
        eastTerrain->Deserialize({{"HeightSamples", PropertyType::String, FlatSamples(33, 33, 5.0f)}});
        auto *eastVolume = east->CreateComponent<TerrainVolumeComponent>();
        eastVolume->SetChunkVoxels(8);

        // Foliage: a tree in the pit, one well outside it.
        auto *trees = ground.scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Trees"}));
        auto *foliage = trees->CreateComponent<FoliageComponent>();
        foliage->AddType("Tree").instances = {FoliageInstance{.id = 1, .position = {132.0f, 5.0f, 66.0f}},
                                             FoliageInstance{.id = 2, .position = {120.0f, 5.0f, 60.0f}}};

        Require(FindTerrainVolumes(ground.scene, {131.0f, 0.0f, 65.0f}, {133.0f, 9.0f, 67.0f}).size() == 2, "Both terrains lie under the seam");
        Require(FindTerrainVolumes(ground.scene, {140.0f, 0.0f, 60.0f}, {141.0f, 9.0f, 61.0f}).size() == 1, "Only the east one lies east");

        const auto report = EditTerrainVolumes(ground.scene, volume::SphereShape({132.0f, 5.0f, 66.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
        Require(report.volumesChanged == 2 && report.result.RockRemoved() > 1.0f, "A pit across the seam digs both terrains");
        Require(report.foliageRemoved == 1 && foliage->GetTotalInstanceCount() == 1, "The tree in the pit goes; the other stays");
        FlushTerrainVolumes(ground.scene);
        Require(ground.terrain->GetEffectiveHoles().IsHole(31, 16) && eastTerrain->GetEffectiveHoles().IsHole(0, 16), "Both sides open");

        // Debris: the removed rock breaks into tagged physics pieces, a few per cubic metre.
        render::Material plain;
        ground.terrain->SetMaterial(&plain);
        TerrainVolumeEditOptions withDebris;
        withDebris.debris.enabled = true;
        withDebris.debris.maxPieces = 4;
        const auto broken = EditTerrainVolumes(ground.scene, volume::SphereShape({110.0f, 5.0f, 60.0f}, 1.2f),
                                               {volume::VolumeEditMode::Subtract}, withDebris);
        Require(broken.Changed() && !broken.debris.empty() && broken.debris.size() <= 4, "A dig with debris breaks the rock it removed");
        for (const auto id : broken.debris)
        {
            const auto *piece = ground.scene.FindEntityByID(id);
            Require(piece && piece->HasTag("Debris") && piece->GetComponent<MeshComponent>() &&
                        piece->GetComponent<MeshComponent>()->GetMaterial() == &plain,
                    "Debris pieces are tagged and draw like the ground (its plain material without layers)");
        }
        const auto fill = EditTerrainVolumes(ground.scene, volume::SphereShape({110.0f, 5.0f, 60.0f}, 1.2f), {volume::VolumeEditMode::Add},
                                             withDebris);
        Require(fill.debris.empty(), "Only digging makes debris");

        const auto tooBig = EditTerrainVolumes(ground.scene, volume::SphereShape({116.0f, 5.0f, 66.0f}, 20.0f), {volume::VolumeEditMode::Subtract});
        Require(tooBig.refused && !tooBig.Changed(), "An edit beyond the budget is refused");
        TerrainVolumeEditOptions tools;
        tools.maxExtent = 0.0f;
        tools.removeFoliage = false;
        Require(EditTerrainVolumes(ground.scene, volume::SphereShape({116.0f, 5.0f, 66.0f}, 20.0f), {volume::VolumeEditMode::Subtract}, tools).Changed(),
                "Without a budget it is not");

        ground.volume->SetEnabled(false);
        Require(FindTerrainVolumes(ground.scene, {131.0f, 0.0f, 65.0f}, {133.0f, 9.0f, 67.0f}).size() == 1, "Disabled volumes are not edited");
    }

    // Phase 5: a save game's edits for every terrain, lone and streamed.
    void SaveState()
    {
        Ground ground;
        EditTerrainVolumes(ground.scene, volume::SphereShape({116.0f, 5.0f, 66.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
        const auto saved = CaptureTerrainVolumeEdits(ground.scene);
        const auto stored = ground.volume->GetVolume()->GetStoredChunks();

        EditTerrainVolumes(ground.scene, volume::SphereShape({104.0f, 5.0f, 54.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
        std::string error;
        Require(RestoreTerrainVolumeEdits(ground.scene, saved, &error) && error.empty(), "Restoring a capture works: " + error);
        Require(ground.volume->GetVolume()->GetStoredChunks() == stored, "The capture's edits come back, later ones go");

        Require(RestoreTerrainVolumeEdits(ground.scene, volume::VolumeEditArchive::Encode({})) &&
                    ground.volume->GetVolume()->GetStoredChunkCount() == 0,
                "A capture without the terrain sends it back to its file");
        auto damaged = saved;
        damaged.pop_back();
        Require(!RestoreTerrainVolumeEdits(ground.scene, damaged, &error) && !error.empty(), "A damaged capture is refused");
    }

    // Phase 5: digging in play survives a tile streaming out and back, and save games carry it; files stay untouched.
    void StreamedEdits()
    {
        const auto directory = TempDirectory();
        TerrainWorldGrid grid;
        grid.tilesX = 2;
        grid.tilesZ = 1;
        grid.tileSamples = 17;
        grid.cellSize = 2.0f;
        grid.heightScale = 50.0f;
        TerrainHeightField field{grid.WorldSamplesX(), grid.WorldSamplesZ(), {}};
        field.samples.assign(static_cast<std::size_t>(field.width) * field.depth, 10.0f);
        const std::string pattern = (directory / "tile_{x}_{z}.plutoheight").string();
        Require(TerrainWorldComponent::WriteWorldFiles(field, grid, pattern, {}, 4).Ok(), "Writing the world");

        Scene scene;
        auto *worldEntity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "World"}));
        auto *world = worldEntity->CreateComponent<TerrainWorldComponent>();
        world->SetGrid(grid);
        world->SetTileFiles(pattern);
        TerrainStreamingSettings streaming;
        streaming.loadRadius = 5.0f;
        streaming.unloadRadius = 10.0f;
        world->SetStreaming(streaming);
        world->EnsureTileTemplate()->CreateComponent<TerrainVolumeComponent>()->SetChunkVoxels(8);
        Require(world->LoadAround({8.0f, 0.0f, 8.0f}, std::chrono::seconds(10)), "Loading tile (0,0)");

        const auto report = EditTerrainVolumes(scene, volume::SphereShape({8.0f, 10.0f, 8.0f}, 2.0f), {volume::VolumeEditMode::Subtract});
        Require(report.volumesChanged == 1, "Digging a loaded tile through the scene");
        Require(world->GetVolumeEditTileCount() == 1, "The world keeps the tile's edits");
        const auto saved = CaptureTerrainVolumeEdits(scene);

        world->UnloadAll();
        Require(world->LoadAround({8.0f, 0.0f, 8.0f}, std::chrono::seconds(10)), "Loading tile (0,0) again");
        auto *volume = world->GetTileEntity({0, 0})->GetComponent<TerrainVolumeComponent>();
        volume->Flush();
        Require(volume->GetVolume()->GetStoredChunkCount() > 0 && world->GetTileTerrain({0, 0})->GetEffectiveHoles().IsHole(4, 4),
                "The pit survives the tile streaming out and back");
        Require(!std::filesystem::exists(directory / "tile_0_0_volume.plutovolume"), "Play never writes the tile's files");

        world->ClearVolumeEdits();
        volume->Flush();
        Require(volume->GetVolume()->GetStoredChunkCount() == 0 && world->GetVolumeEditTileCount() == 0, "Clearing goes back to the files");

        std::string error;
        Require(RestoreTerrainVolumeEdits(scene, saved, &error), "Restoring the save: " + error);
        volume->Flush();
        Require(volume->GetVolume()->GetStoredChunkCount() > 0 && world->GetTileTerrain({0, 0})->GetEffectiveHoles().IsHole(4, 4),
                "A save game brings the pit back to a loaded tile");
        world->UnloadAll();
        Require(world->GetVolumeEditTileCount() == 1, "and keeps it for the tile while it is away");

        // Foliage dug away on a tile stays away: remembered per tile, saved with the edits.
        const std::vector<std::uint64_t> plants{ProceduralFoliageInstanceIdBit | 7, ProceduralFoliageInstanceIdBit | 9};
        world->SuppressFoliage({1, 0}, plants);
        Require(world->GetSuppressedFoliageCount({1, 0}) == 2, "The world remembers dug-away plants");
        const auto withPlants = world->ExportVolumeEdits();
        world->ClearVolumeEdits();
        Require(world->GetSuppressedFoliageCount({1, 0}) == 0, "Clearing forgets them");
        Require(world->ImportVolumeEdits(withPlants) && world->GetSuppressedFoliageCount({1, 0}) == 2, "and a save brings them back");
        volume::VolumeEditArchive::Entries broken;
        broken["foliage:1,0"] = {1, 2, 3};
        Require(!world->ImportVolumeEdits(volume::VolumeEditArchive::Encode(broken)) && world->GetSuppressedFoliageCount({1, 0}) == 2,
                "A damaged foliage entry is refused and changes nothing");
    }
}

int main()
{
    try
    {
        DigAPit();
        CaveUnderground();
        Files();
        Settings();
        WorldTiles();
        LayeredChunks();
        EditsInAndOut();
        SaveState();
        SceneEdits();
        StreamedEdits();
        ProceduralCaves();
        Stamps();
        FarTiles();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Terrain volume component tests passed.\n";
    return 0;
}
