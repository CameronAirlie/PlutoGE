#pragma once

#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>

#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    class ColliderComponent;
    class Entity;
}

namespace PlutoGE::scene::detail
{
    // A Bullet collision shape and everything its raw pointers refer to: the children of a compound, the triangle
    // storage behind a mesh shape and the samples behind a heightfield. Declaration order is destruction order in
    // reverse, so the shape goes before the triangle storage it reads.
    struct BulletShapeData
    {
        std::vector<std::unique_ptr<btTriangleMesh>> ownedTriangleMeshes;
        std::unique_ptr<btCollisionShape> shape;
        std::vector<std::unique_ptr<btCollisionShape>> ownedChildShapes;
        std::vector<float> ownedHeightfieldData;
    };

    // A finished collider shape that any number of collision objects, in any number of Bullet worlds, may share.
    // Bullet takes shapes by non-const pointer, but a shape is never changed once built: a collider whose geometry
    // changes gets a new shape instead.
    class BulletCollisionShape
    {
    public:
        explicit BulletCollisionShape(BulletShapeData data) noexcept : m_data(std::move(data)) {}
        BulletCollisionShape(const BulletCollisionShape &) = delete;
        BulletCollisionShape &operator=(const BulletCollisionShape &) = delete;

        [[nodiscard]] btCollisionShape *Get() const noexcept { return m_data.shape.get(); }

    private:
        BulletShapeData m_data;
    };

    using SharedBulletShape = std::shared_ptr<const BulletCollisionShape>;

    // Builds the shape of an entity's collider, with the entity's world scale applied. The result is empty while the
    // collider has no geometry (a mesh collider whose mesh has not loaded). Nothing is shared or remembered.
    [[nodiscard]] BulletShapeData BuildColliderShape(const Entity &entity, const ColliderComponent &collider);

    // Hands out collider shapes for the scene's Bullet worlds (queries, simulation, triggers).
    //
    // Geometry that is expensive to build (triangle meshes, terrain) is shared: colliders with the same source
    // geometry at the same scale get one shape, and a body recreated for an unrelated reason (a spawn, a rigidbody
    // setting, a world rebuild) gets its existing shape back instead of rebuilding a BVH. A shared shape lives as long
    // as some body holds it; the cache only remembers it, so nothing outlives its last user.
    //
    // A source is identified by what the geometry was built from plus that source's content revision, so an edit
    // (a terrain's heights or holes, a mesh's vertices) yields a new shape. To share a new kind of geometry, describe
    // its source in DescribeSharedSource (BulletCollisionShapes.cpp); every other collider is built on each request.
    class BulletShapeCache
    {
    public:
        [[nodiscard]] SharedBulletShape Acquire(const Entity &entity, const ColliderComponent &collider);

        // Shapes built since the cache was created, shared or not. Diagnostics and tests use it to see how much
        // geometry a synchronization had to build.
        [[nodiscard]] std::uint64_t GetBuiltShapeCount() const noexcept { return m_builtShapeCount; }
        // Shared shapes that at least one body still holds.
        [[nodiscard]] std::size_t GetSharedShapeCount() const;

        // Identifies shareable geometry exactly; no hashing shortcuts, so two sources can never be confused.
        struct SourceKey
        {
            enum class Kind : std::uint8_t
            {
                Mesh,
                Terrain,
            };

            Kind kind = Kind::Mesh;
            // The geometry's owner: a mesh's address or a terrain entity's ID.
            std::uint64_t identity = 0;
            // Everything else the shape is built from (revisions, dimensions), as each kind defines it.
            std::array<std::uint64_t, 4> content{};
            glm::vec3 worldScale{1.0f};

            bool operator==(const SourceKey &) const = default;
        };

    private:
        struct SourceKeyHash
        {
            std::size_t operator()(const SourceKey &key) const noexcept;
        };

        struct Entry
        {
            // Set when the source can be freed and replaced at the same address (a mesh): an entry is only reused
            // while the very same source object is alive.
            std::weak_ptr<const void> sourceLifetime;
            std::weak_ptr<const BulletCollisionShape> shape;
        };

        SharedBulletShape Build(const Entity &entity, const ColliderComponent &collider);
        void PruneExpiredEntries();

        std::unordered_map<SourceKey, Entry, SourceKeyHash> m_sharedShapes;
        std::uint64_t m_builtShapeCount = 0;
    };
}
