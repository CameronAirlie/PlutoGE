#include "PlutoGE/scene/TerrainVolumeEditing.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/TerrainVolumeComponent.h"
#include "PlutoGE/scene/components/TerrainWorldComponent.h"
#include "PlutoGE/scene/volume/VolumeEditArchive.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <functional>
#include <limits>

namespace PlutoGE::scene
{
    namespace
    {
        void ForEachEntity(Scene &scene, const std::function<void(Entity &)> &visit)
        {
            std::vector<Entity *> pending(scene.GetRootEntities().rbegin(), scene.GetRootEntities().rend());
            while (!pending.empty())
            {
                Entity *entity = pending.back();
                pending.pop_back();
                if (!entity)
                    continue;
                visit(*entity);
                pending.insert(pending.end(), entity->GetChildren().rbegin(), entity->GetChildren().rend());
            }
        }

        // The terrain's footprint in the world, from above.
        bool CoversFromAbove(const Entity &entity, const TerrainComponent &terrain, const glm::vec3 &worldMin, const glm::vec3 &worldMax)
        {
            const glm::vec2 extent(std::max(1, terrain.GetWidth() - 1) * terrain.GetCellSize(),
                                   std::max(1, terrain.GetDepth() - 1) * terrain.GetCellSize());
            const glm::mat4 world = entity.GetWorldTransform();
            glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
            for (int corner = 0; corner < 4; ++corner)
            {
                const glm::vec3 local((corner & 1) ? extent.x : 0.0f, 0.0f, (corner & 2) ? extent.y : 0.0f);
                const glm::vec3 point(world * glm::vec4(local, 1.0f));
                low = glm::min(low, glm::vec2(point.x, point.z));
                high = glm::max(high, glm::vec2(point.x, point.z));
            }
            return low.x <= worldMax.x && high.x >= worldMin.x && low.y <= worldMax.z && high.y >= worldMin.z;
        }

        // The entity's names from the scene root; siblings of the same name are told apart by their order.
        std::string EntityPath(const Entity &entity)
        {
            std::string path;
            for (const Entity *at = &entity; at; at = at->GetParent())
            {
                std::string part = at->GetName();
                if (const Entity *parent = at->GetParent())
                {
                    int index = 0;
                    for (const Entity *sibling : parent->GetChildren())
                    {
                        if (sibling == at)
                            break;
                        index += sibling && sibling->GetName() == part ? 1 : 0;
                    }
                    if (index > 0)
                        part += "[" + std::to_string(index) + "]";
                }
                path = path.empty() ? part : part + "/" + path;
            }
            return path;
        }

        // Volumes that belong to the scene itself: not a world's tile (the world keeps those) or its template.
        bool IsLoneVolume(const Entity &entity)
        {
            return !entity.IsTransientInHierarchy() && !TerrainWorldComponent::IsTileTemplate(entity);
        }

        constexpr std::string_view WorldPrefix = "world:";
        constexpr std::string_view VolumePrefix = "volume:";

        // The world and tile a foliage component's entity is, when it is a streamed tile.
        std::pair<TerrainWorldComponent *, std::optional<TerrainTileCoord>> TileOf(Entity &entity)
        {
            for (Entity *at = entity.GetParent(); at; at = at->GetParent())
                if (auto *world = at->GetComponent<TerrainWorldComponent>())
                    return {world, world->FindTile(entity)};
            return {nullptr, std::nullopt};
        }

        std::size_t RemoveCarvedFoliage(Scene &scene, const volume::VolumeShape &worldShape)
        {
            const glm::vec3 centre = (worldShape.BoundsMin() + worldShape.BoundsMax()) * 0.5f;
            const float radius = glm::length(worldShape.BoundsMax() - centre);
            std::size_t removed = 0;
            ForEachEntity(scene, [&](Entity &entity) {
                auto *foliage = entity.GetComponent<FoliageComponent>();
                if (!foliage)
                    return;
                std::vector<std::uint64_t> carved;
                for (const auto &location : foliage->FindInstancesInRadius(centre, radius))
                {
                    const auto transform = foliage->GetInstanceWorldTransform(location);
                    const auto *instance = foliage->GetInstance(location);
                    if (transform && instance && worldShape.Distance(glm::vec3((*transform)[3])) <= 0.0f)
                        carved.push_back(instance->id);
                }
                if (carved.empty())
                    return;
                removed += foliage->RemoveInstances(carved).size();
                // A tile regenerates its foliage when it streams in again: its world remembers what was dug away.
                if (const auto [world, tile] = TileOf(entity); world && tile)
                    world->SuppressFoliage(*tile, carved);
            });
            return removed;
        }
    }

    std::vector<TerrainVolumeComponent *> FindTerrainVolumes(Scene &scene, const glm::vec3 &worldMin, const glm::vec3 &worldMax)
    {
        std::vector<TerrainVolumeComponent *> found;
        ForEachEntity(scene, [&](Entity &entity) {
            auto *terrainVolume = entity.GetComponent<TerrainVolumeComponent>();
            const auto *terrain = entity.GetComponent<TerrainComponent>();
            if (terrainVolume && terrain && terrainVolume->IsEnabled() && entity.IsActiveInHierarchy() &&
                !TerrainWorldComponent::IsTileTemplate(entity) && CoversFromAbove(entity, *terrain, worldMin, worldMax))
                found.push_back(terrainVolume);
        });
        return found;
    }

    TerrainVolumeEditReport EditTerrainVolumes(Scene &scene, const volume::VolumeShape &worldShape, const volume::VolumeEdit &edit,
                                               const TerrainVolumeEditOptions &options)
    {
        TerrainVolumeEditReport report;
        const glm::vec3 size = worldShape.BoundsMax() - worldShape.BoundsMin();
        if (options.maxExtent > 0.0f && std::max({size.x, size.y, size.z}) > options.maxExtent)
        {
            report.refused = true;
            return report;
        }
        // Neighbouring volumes (tiles) each take the part over their own terrain; the rest of the shape is air to them.
        for (auto *terrainVolume : FindTerrainVolumes(scene, worldShape.BoundsMin(), worldShape.BoundsMax()))
        {
            if (options.accept && !options.accept(*terrainVolume))
                continue;
            // The rock about to go, while it is still there.
            const bool debris = options.debris.enabled && edit.mode == volume::VolumeEditMode::Subtract;
            glm::vec3 debrisOrigin(0.0f);
            auto removed = debris ? terrainVolume->MeshRemovedRock(worldShape, debrisOrigin) : volume::VolumeMesh{};
            const auto result = terrainVolume->Apply(worldShape, edit);
            if (!result.Changed())
                continue;
            report.result.Merge(result);
            ++report.volumesChanged;
            const float rock = result.RockRemoved();
            if (debris && rock >= options.debris.minimumRock && !removed.Empty())
            {
                auto settings = options.debris;
                settings.shatter.pieces = std::clamp(static_cast<int>(std::ceil(rock * settings.piecesPerCubicMetre)), 1,
                                                     std::max(settings.maxPieces, 1));
                const glm::vec3 impact = (worldShape.BoundsMin() + worldShape.BoundsMax()) * 0.5f;
                for (auto *piece : SpawnTerrainDebris(scene, *terrainVolume, std::move(removed), debrisOrigin, impact, settings))
                    report.debris.push_back(piece->GetID());
            }
        }
        if (report.Changed() && options.removeFoliage && edit.mode == volume::VolumeEditMode::Subtract)
            report.foliageRemoved = RemoveCarvedFoliage(scene, worldShape);
        return report;
    }

    void FlushTerrainVolumes(Scene &scene)
    {
        ForEachEntity(scene, [](Entity &entity) {
            if (auto *terrainVolume = entity.GetComponent<TerrainVolumeComponent>())
                if (!TerrainWorldComponent::IsTileTemplate(entity))
                    terrainVolume->Flush();
        });
    }

    std::vector<std::uint8_t> CaptureTerrainVolumeEdits(Scene &scene)
    {
        volume::VolumeEditArchive::Entries entries;
        ForEachEntity(scene, [&](Entity &entity) {
            if (auto *world = entity.GetComponent<TerrainWorldComponent>())
                entries.insert_or_assign(std::string(WorldPrefix) + EntityPath(entity), world->ExportVolumeEdits());
            if (auto *terrainVolume = entity.GetComponent<TerrainVolumeComponent>(); terrainVolume && IsLoneVolume(entity))
                if (auto bytes = terrainVolume->ExportEdits(); !bytes.empty())
                    entries.insert_or_assign(std::string(VolumePrefix) + EntityPath(entity), std::move(bytes));
        });
        return volume::VolumeEditArchive::Encode(entries);
    }

    bool RestoreTerrainVolumeEdits(Scene &scene, std::span<const std::uint8_t> bytes, std::string *error)
    {
        auto entries = volume::VolumeEditArchive::Decode(bytes, error);
        if (!entries)
            return false;
        // Worlds' entries are archives themselves: checked before anything changes.
        for (const auto &[name, data] : *entries)
            if (name.starts_with(WorldPrefix) && !volume::VolumeEditArchive::Decode(data, error))
                return false;

        std::string problems;
        const auto note = [&](const std::string &name, const std::string &reason) {
            problems += (problems.empty() ? "" : "\n") + name + ": " + reason;
        };
        ForEachEntity(scene, [&](Entity &entity) {
            if (auto *world = entity.GetComponent<TerrainWorldComponent>())
            {
                const auto name = std::string(WorldPrefix) + EntityPath(entity);
                const auto found = entries->find(name);
                std::string reason;
                if (found != entries->end() ? !world->ImportVolumeEdits(found->second, &reason) : (world->ClearVolumeEdits(), false))
                    note(name, reason);
            }
            if (auto *terrainVolume = entity.GetComponent<TerrainVolumeComponent>(); terrainVolume && IsLoneVolume(entity))
            {
                const auto name = std::string(VolumePrefix) + EntityPath(entity);
                const auto found = entries->find(name);
                std::string reason;
                const bool ok = found != entries->end() ? terrainVolume->ImportEdits(found->second, &reason) : terrainVolume->ReloadFile(&reason);
                if (!ok)
                    note(name, reason);
            }
        });
        // The blob was sound; parts that no longer fit (a terrain's grid changed since) are reported, the rest restored.
        if (error)
            *error = problems;
        return true;
    }
}
