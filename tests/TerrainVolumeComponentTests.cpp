// TerrainVolumeComponent in a scene (docs/dev/TERRAIN_VOLUMES_PLAN.md, phase 3): world-space edits, chunk entities with
// mesh colliders, derived holes kept apart from authored ones, collision through a dug pit, enabling and removing,
// the volume file, and world tiles that keep their own volume file.
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/TerrainWorldGrid.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
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

int main()
{
    try
    {
        DigAPit();
        CaveUnderground();
        Files();
        Settings();
        WorldTiles();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Terrain volume component tests passed.\n";
    return 0;
}
