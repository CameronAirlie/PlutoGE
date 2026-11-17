#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;
    class Scene;

    // How Shatter breaks something. Distances are metres, in world space.
    struct ShatterSettings
    {
        // Pieces for the largest mesh in the subtree; smaller meshes get fewer, by volume, at least one.
        int pieces = 8;
        std::uint32_t seed = 0;
        // Where the blow landed: seeds are pulled towards it by impactBias (0..1), so pieces are smaller there.
        std::optional<glm::vec3> impactPoint;
        // Added to every piece's velocity, scaled by impactSpeed. Zero for none.
        glm::vec3 impactDirection{0.0f};
        float impactBias = 0.5f;
        // kg/m³: a piece weighs its volume times this (at least kMinShatterPieceMass).
        float density = 2600.0f;
        // m/s outward from the centre of the source's bounds.
        float burstSpeed = 2.5f;
        // m/s along impactDirection.
        float impactSpeed = 1.5f;
        // rad/s: each piece spins about a random axis at up to this rate.
        float spin = 4.0f;
        // Pieces whose largest side is shorter than this are left out.
        float minPieceSize = 0.04f;
        // Given to every piece; empty for none.
        std::string tag;
    };

    // One shatter spawns at most this many pieces, whatever the subtree holds.
    inline constexpr std::size_t kMaxShatterPieces = 1024;
    inline constexpr float kMinShatterPieceMass = 0.05f;

    // Breaks every active, visible mesh in root's subtree (root included) into Voronoi pieces (MeshFracture.h) and
    // spawns each piece as a root entity named "<source name> piece N": unit scale, placed at the piece's centre of
    // mass with the source mesh's world rotation, drawing exactly where the source was drawn. Every piece has a
    // MeshComponent with runtime geometry and the source's materials (shared, slot for slot), a ConvexHull collider and
    // a dynamic rigidbody moving outward. The source is left as it was: hiding or destroying it is the caller's choice.
    // Returns the pieces (empty when nothing in the subtree could be broken). See docs/FRACTURE.md.
    std::vector<Entity *> Shatter(Scene &scene, Entity &root, const ShatterSettings &settings);
}
