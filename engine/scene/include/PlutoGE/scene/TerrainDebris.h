#pragma once

#include "PlutoGE/scene/Shatter.h"
#include "PlutoGE/scene/volume/VolumeMesher.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
    class Scene;
    class TerrainVolumeComponent;

    // Debris from dug ground (docs/dev/TERRAIN_VOLUMES_PLAN.md): the rock a dig removed, broken by Shatter into physics
    // pieces that look like the rock they came from. Gameplay chooses whether a dig makes any
    // (TerrainVolumeEditOptions::debris) and what happens to the pieces (they carry `shatter.tag`).
    struct TerrainDebrisSettings
    {
        bool enabled = false;
        // Pieces per cubic metre removed, at least one and at most maxPieces.
        float piecesPerCubicMetre = 8.0f;
        int maxPieces = 10;
        // Digs removing less rock than this (cubic metres) make no debris.
        float minimumRock = 0.02f;
        ShatterSettings shatter{.pieces = 6, .density = 1600.0f, .burstSpeed = 1.5f, .impactSpeed = 1.0f, .spin = 3.0f,
                                .minPieceSize = 0.05f, .tag = "Debris"};
    };

    // Breaks a removed-rock mesh (volume::MeshRemovedRock, positions relative to `localOrigin` in the volume's space)
    // into pieces drawn with the volume's layer of the rock most of it was. Returns the pieces (none when nothing could
    // be broken). The material lives for the process: pieces outlive the terrain tile they came from.
    std::vector<Entity *> SpawnTerrainDebris(Scene &scene, TerrainVolumeComponent &volume, volume::VolumeMesh removed,
                                             const glm::vec3 &localOrigin, const glm::vec3 &worldImpact,
                                             const TerrainDebrisSettings &settings);
}
