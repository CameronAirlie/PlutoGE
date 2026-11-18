#pragma once

#include "PlutoGE/scene/volume/TerrainVolume.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Scene;
    class TerrainVolumeComponent;

    // Terrain volume edits for gameplay (docs/dev/TERRAIN_VOLUMES_PLAN.md, phase 5): a world-space shape goes to every
    // terrain volume under it, so digging across a world's tile seam, or where two terrains meet, needs no knowledge of
    // tiles. Scripts reach this through the TerrainVolumes C# API; tools that edit one terrain call its component.

    struct TerrainVolumeEditOptions
    {
        // A runtime budget: shapes wider than this on any axis (metres) are refused, so one call cannot stall a frame
        // re-sampling and re-meshing hundreds of chunks. 0 accepts any size (tools).
        float maxExtent = 32.0f;
        // Subtract edits remove the foliage instances whose pivot they carve out (trees left floating over a pit).
        bool removeFoliage = true;
        // Which volumes the edit may change (all when empty): an editor brush keeps to the terrain it is used on.
        std::function<bool(const TerrainVolumeComponent &)> accept;
    };

    struct TerrainVolumeEditReport
    {
        // Every volume's result together (chunk coordinates are each volume's own; see rockRemoved / rockAdded).
        volume::VolumeEditResult result;
        int volumesChanged = 0;
        std::size_t foliageRemoved = 0;
        bool refused = false; // the shape was larger than maxExtent
        [[nodiscard]] bool Changed() const { return volumesChanged > 0; }
    };

    // The enabled terrain volumes (lone terrains and loaded world tiles, never a world's tile template) whose terrain
    // covers part of a world-space box, looked at from above (volumes reach any height).
    [[nodiscard]] std::vector<TerrainVolumeComponent *> FindTerrainVolumes(Scene &scene, const glm::vec3 &worldMin, const glm::vec3 &worldMax);

    TerrainVolumeEditReport EditTerrainVolumes(Scene &scene, const volume::VolumeShape &worldShape, const volume::VolumeEdit &edit,
                                               const TerrainVolumeEditOptions &options = {});

    // Lands every volume's pending meshes and holes now (after a load, before a screenshot).
    void FlushTerrainVolumes(Scene &scene);

    // ---- save games ----------------------------------------------------------------------------------------------------
    // Every terrain's volume edits as one blob (volume::VolumeEditArchive): each world's kept tile edits and each lone
    // volume's stored chunks, named by the entity's path from the scene root. Restoring gives each its own back; one
    // the blob does not name goes back to its files. False (with a reason) when the blob is damaged: nothing changes.
    [[nodiscard]] std::vector<std::uint8_t> CaptureTerrainVolumeEdits(Scene &scene);
    bool RestoreTerrainVolumeEdits(Scene &scene, std::span<const std::uint8_t> bytes, std::string *error = nullptr);
}
