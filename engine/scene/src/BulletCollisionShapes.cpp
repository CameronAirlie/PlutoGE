#include "BulletCollisionShapes.h"

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/TerrainHoleMask.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <btBulletCollisionCommon.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include <BulletCollision/CollisionShapes/btShapeHull.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <optional>

namespace PlutoGE::scene::detail
{
    namespace
    {
        btVector3 ToBullet(const glm::vec3 &value)
        {
            return btVector3(value.x, value.y, value.z);
        }

        std::unique_ptr<btCollisionShape> CreatePrimitiveShape(const ColliderComponent &collider, const glm::vec3 &worldScale)
        {
            switch (collider.GetShape())
            {
            case ColliderShape::Sphere:
            {
                const float radius = collider.GetScaledRadius(worldScale);
                return std::make_unique<btSphereShape>(std::max(radius, 0.0001f));
            }
            case ColliderShape::Capsule:
            {
                const float radius = collider.GetScaledRadius(worldScale);
                const float cylinderHeight = std::max(collider.GetScaledHeight(worldScale) - radius * 2.0f, 0.0f);
                return std::make_unique<btCapsuleShape>(std::max(radius, 0.0001f), cylinderHeight);
            }
            case ColliderShape::Box:
            default:
            {
                const glm::vec3 halfExtents = glm::max(collider.GetScaledSize(worldScale) * 0.5f, glm::vec3(0.0001f));
                return std::make_unique<btBoxShape>(ToBullet(halfExtents));
            }
            }
        }

        // A primitive offset by the collider's centre sits in a compound, which carries the offset.
        BulletShapeData CreatePrimitiveColliderShape(const ColliderComponent &collider, const glm::vec3 &worldScale)
        {
            BulletShapeData shapeData;
            auto baseShape = CreatePrimitiveShape(collider, worldScale);
            if (!baseShape)
                return shapeData;

            const glm::vec3 scaledCenter = collider.GetScaledCenter(worldScale);
            if (glm::dot(scaledCenter, scaledCenter) <= 0.0000001f)
            {
                shapeData.shape = std::move(baseShape);
                return shapeData;
            }

            auto compoundShape = std::make_unique<btCompoundShape>();
            btTransform childTransform;
            childTransform.setIdentity();
            childTransform.setOrigin(ToBullet(scaledCenter));
            compoundShape->addChildShape(childTransform, baseShape.get());

            shapeData.ownedChildShapes.push_back(std::move(baseShape));
            shapeData.shape = std::move(compoundShape);
            return shapeData;
        }

        // The mesh's own vertex and index buffers, scaled. Vertices are copied as they are rather than welded: a
        // BVH triangle mesh does not need shared vertices, and welding with btTriangleMesh's duplicate search is
        // quadratic in the vertex count (about 90 ms for a 10k-triangle rock).
        BulletShapeData CreateMeshShape(const render::Mesh &mesh, const glm::vec3 &worldScale)
        {
            const auto &meshData = mesh.GetMeshData();
            const auto vertexCount = meshData.vertices.size();
            if (vertexCount == 0 || meshData.indices.size() < 3 ||
                vertexCount > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                return {};

            auto triangleMesh = std::make_unique<btTriangleMesh>(true, false);
            triangleMesh->preallocateVertices(static_cast<int>(vertexCount));
            triangleMesh->preallocateIndices(static_cast<int>(meshData.indices.size()));
            for (const auto &vertex : meshData.vertices)
                triangleMesh->findOrAddVertex(btVector3(vertex.position[0] * worldScale.x,
                                                        vertex.position[1] * worldScale.y,
                                                        vertex.position[2] * worldScale.z),
                                              false);
            for (std::size_t index = 0; index + 2 < meshData.indices.size(); index += 3)
            {
                const auto i0 = meshData.indices[index + 0];
                const auto i1 = meshData.indices[index + 1];
                const auto i2 = meshData.indices[index + 2];
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
                    continue;
                triangleMesh->addTriangleIndices(static_cast<int>(i0), static_cast<int>(i1), static_cast<int>(i2));
            }
            if (triangleMesh->getNumTriangles() == 0)
                return {};

            BulletShapeData shapeData;
            shapeData.shape = std::make_unique<btBvhTriangleMeshShape>(triangleMesh.get(), true);
            shapeData.ownedTriangleMeshes.push_back(std::move(triangleMesh));
            return shapeData;
        }

        // The convex hull of a mesh's vertices, scaled, for bodies that move. Bullet's hull is a support-mapped point
        // cloud, so its cost per contact grows with the point count: inputs with more than kMaxHullPoints points are
        // reduced with btShapeHull (support points in a fixed set of directions, at most a few dozen). The hull keeps
        // the mesh's origin, which is the body's centre of mass; fracture pieces are authored centred on it.
        BulletShapeData CreateConvexHullShape(const render::Mesh &mesh, const glm::vec3 &worldScale)
        {
            constexpr int kMaxHullPoints = 64;
            const auto &vertices = mesh.GetMeshData().vertices;
            if (vertices.size() < 4)
                return {};

            auto hull = std::make_unique<btConvexHullShape>();
            btVector3 minimum(BT_LARGE_FLOAT, BT_LARGE_FLOAT, BT_LARGE_FLOAT);
            btVector3 maximum(-BT_LARGE_FLOAT, -BT_LARGE_FLOAT, -BT_LARGE_FLOAT);
            for (const auto &vertex : vertices)
            {
                const btVector3 point(vertex.position[0] * worldScale.x, vertex.position[1] * worldScale.y,
                                      vertex.position[2] * worldScale.z);
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(point.z()))
                    continue;
                hull->addPoint(point, false);
                minimum.setMin(point);
                maximum.setMax(point);
            }
            const btVector3 extent = maximum - minimum;
            const btScalar thinnest = extent[extent.minAxis()];
            if (hull->getNumPoints() < 4 || !(thinnest > 0.0001f))
                return {};

            // Bullet inflates convex shapes by their margin (default 4 cm), which floats small debris visibly above
            // the ground. Keep it a small fraction of the thinnest side instead.
            const btScalar margin = std::clamp(thinnest * 0.05f, btScalar(0.001f), btScalar(0.01f));
            if (hull->getNumPoints() > kMaxHullPoints)
            {
                hull->setMargin(0.0f); // btShapeHull samples support points, margin included.
                hull->recalcLocalAabb();
                btShapeHull reduced(hull.get());
                if (reduced.buildHull(0.0f) && reduced.numVertices() >= 4)
                {
                    auto simplified = std::make_unique<btConvexHullShape>(
                        reinterpret_cast<const btScalar *>(reduced.getVertexPointer()), reduced.numVertices(), static_cast<int>(sizeof(btVector3)));
                    hull = std::move(simplified);
                }
            }
            hull->setMargin(margin);
            hull->recalcLocalAabb();

            BulletShapeData shapeData;
            shapeData.shape = std::move(hull);
            return shapeData;
        }

        // A heightfield child over samples [x0, x0 + cellsX] x [z0, z0 + cellsZ] of a terrain, its samples owned by
        // `shapeData`, placed in the terrain's local space (scaled). Bullet centres a heightfield on its bounds.
        void AddHeightfieldChild(BulletShapeData &shapeData, btCompoundShape &compound, const std::vector<float> &heights, int width,
                                 int x0, int z0, int cellsX, int cellsZ, const btVector3 &scale)
        {
            std::vector<float> block;
            block.reserve(static_cast<std::size_t>(cellsX + 1) * static_cast<std::size_t>(cellsZ + 1));
            float minHeight = std::numeric_limits<float>::max();
            float maxHeight = std::numeric_limits<float>::lowest();
            for (int z = z0; z <= z0 + cellsZ; ++z)
                for (int x = x0; x <= x0 + cellsX; ++x)
                {
                    const float height = heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                    block.push_back(height);
                    minHeight = std::min(minHeight, height);
                    maxHeight = std::max(maxHeight, height);
                }
            if (!std::isfinite(minHeight) || !std::isfinite(maxHeight))
                return;
            if (std::abs(maxHeight - minHeight) <= 0.0001f)
                maxHeight = minHeight + 0.0001f;
            auto shape = std::make_unique<btHeightfieldTerrainShape>(cellsX + 1, cellsZ + 1, block.data(), 1.0f, minHeight, maxHeight, 1,
                                                                    PHY_FLOAT, false);
            shape->setLocalScaling(scale);
            // The (x+1,z)-(x,z+1) diagonal, as the whole terrain's heightfield (see CreateTerrainShape).
            shape->setUseDiamondSubdivision(false);
            btTransform transform;
            transform.setIdentity();
            transform.setOrigin(btVector3(static_cast<btScalar>((x0 + cellsX * 0.5f) * scale.x()),
                                          static_cast<btScalar>((minHeight + maxHeight) * 0.5f * scale.y()),
                                          static_cast<btScalar>((z0 + cellsZ * 0.5f) * scale.z())));
            compound.addChildShape(transform, shape.get());
            shapeData.ownedHeightfieldBlocks.push_back(std::move(block));
            shapeData.ownedChildShapes.push_back(std::move(shape));
        }

        // A terrain with holes. Bullet's heightfield cannot leave cells out (and its accelerated ray test cannot be
        // overridden), so the terrain is split into blocks: solid blocks stay heightfields, and blocks holding holes become
        // triangle meshes of their solid cells, split along the heightfield's (x+1,z)-(x,z+1) diagonal like the rendered
        // mesh. Holes are local (a cave mouth, a dug pit), so a tile streaming in with a few of them builds a few small
        // BVHs rather than one over every cell (about 90 ms for a 257-sample tile).
        BulletShapeData CreateHoledTerrainShape(const TerrainComponent &terrain, const glm::vec3 &worldScale)
        {
            constexpr int BlockCells = 32;
            const int width = terrain.GetWidth();
            const int depth = terrain.GetDepth();
            const auto &heights = terrain.GetHeightSamples();
            const auto &holes = terrain.GetEffectiveHoles();
            const btVector3 scale(std::max(terrain.GetCellSize() * std::abs(worldScale.x), 0.0001f), std::max(std::abs(worldScale.y), 0.0001f),
                                  std::max(terrain.GetCellSize() * std::abs(worldScale.z), 0.0001f));

            BulletShapeData shapeData;
            auto compound = std::make_unique<btCompoundShape>();
            for (int z0 = 0; z0 + 1 < depth; z0 += BlockCells)
                for (int x0 = 0; x0 + 1 < width; x0 += BlockCells)
                {
                    const int cellsX = std::min(BlockCells, width - 1 - x0);
                    const int cellsZ = std::min(BlockCells, depth - 1 - z0);
                    if (!holes.AnyInRect(x0, z0, x0 + cellsX, z0 + cellsZ))
                    {
                        AddHeightfieldChild(shapeData, *compound, heights, width, x0, z0, cellsX, cellsZ, scale);
                        continue;
                    }
                    auto triangleMesh = std::make_unique<btTriangleMesh>(true, false);
                    std::vector<int> vertexOf(static_cast<std::size_t>(cellsX + 1) * static_cast<std::size_t>(cellsZ + 1), -1);
                    const auto vertex = [&](int x, int z) {
                        auto &index = vertexOf[static_cast<std::size_t>(z - z0) * static_cast<std::size_t>(cellsX + 1) + static_cast<std::size_t>(x - x0)];
                        if (index < 0)
                        {
                            const float height = heights[static_cast<std::size_t>(z) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                            index = triangleMesh->findOrAddVertex(btVector3(static_cast<btScalar>(x * scale.x()),
                                                                            static_cast<btScalar>(height * scale.y()),
                                                                            static_cast<btScalar>(z * scale.z())),
                                                                  false);
                        }
                        return index;
                    };
                    for (int z = z0; z < z0 + cellsZ; ++z)
                        for (int x = x0; x < x0 + cellsX; ++x)
                        {
                            const auto triangles = TerrainCellTriangles(holes.GetCut(x, z));
                            if (triangles.empty())
                                continue;
                            const int corners[4] = {vertex(x, z), vertex(x + 1, z), vertex(x, z + 1), vertex(x + 1, z + 1)};
                            for (const auto &triangle : triangles)
                                triangleMesh->addTriangleIndices(corners[triangle[0]], corners[triangle[1]], corners[triangle[2]]);
                        }
                    if (triangleMesh->getNumTriangles() == 0)
                        continue;
                    auto shape = std::make_unique<btBvhTriangleMeshShape>(triangleMesh.get(), true);
                    btTransform identity;
                    identity.setIdentity();
                    compound->addChildShape(identity, shape.get());
                    shapeData.ownedTriangleMeshes.push_back(std::move(triangleMesh));
                    shapeData.ownedChildShapes.push_back(std::move(shape));
                }
            if (compound->getNumChildShapes() == 0)
                return {};
            shapeData.shape = std::move(compound);
            return shapeData;
        }

        BulletShapeData CreateTerrainShape(const TerrainComponent &terrain, const glm::vec3 &worldScale)
        {
            BulletShapeData shapeData;
            const int width = terrain.GetWidth();
            const int depth = terrain.GetDepth();
            const auto &heightSamples = terrain.GetHeightSamples();
            const std::size_t expectedHeightCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(depth);
            if (width < 2 || depth < 2 || heightSamples.size() != expectedHeightCount)
                return shapeData;
            if (!terrain.GetEffectiveHoles().Empty())
                return CreateHoledTerrainShape(terrain, worldScale);

            shapeData.ownedHeightfieldData = heightSamples;
            float minHeight = std::numeric_limits<float>::max();
            float maxHeight = std::numeric_limits<float>::lowest();
            for (const float height : shapeData.ownedHeightfieldData)
            {
                minHeight = std::min(minHeight, height);
                maxHeight = std::max(maxHeight, height);
            }
            if (!std::isfinite(minHeight) || !std::isfinite(maxHeight))
                return {};
            if (std::abs(maxHeight - minHeight) <= 0.0001f)
                maxHeight = minHeight + 0.0001f;

            auto shape = std::make_unique<btHeightfieldTerrainShape>(
                width,
                depth,
                shapeData.ownedHeightfieldData.data(),
                1.0f,
                minHeight,
                maxHeight,
                1,
                PHY_FLOAT,
                false);

            const float scaleX = std::max(terrain.GetCellSize() * std::abs(worldScale.x), 0.0001f);
            const float scaleY = std::max(std::abs(worldScale.y), 0.0001f);
            const float scaleZ = std::max(terrain.GetCellSize() * std::abs(worldScale.z), 0.0001f);
            shape->setLocalScaling(btVector3(scaleX, scaleY, scaleZ));
            // Keep Bullet's default quad split, the (x+1,z)-(x,z+1) diagonal TerrainComponent renders. With diamond
            // subdivision this Bullet version ray-casts each cell with the opposite diagonal to the one convex sweeps
            // and contacts collide with (performRaycast tests odd x+z parity, processAllTriangles even), so rays and
            // capsules disagreed by 0.1-0.3 m (up to 1 m) in most cells of rough terrain: ground probes, foot IK and character collision
            // each saw a different surface, and neither matched the rendered one.
            shape->setUseDiamondSubdivision(false);

            auto compoundShape = std::make_unique<btCompoundShape>();
            btTransform childTransform;
            childTransform.setIdentity();
            childTransform.setOrigin(btVector3(
                static_cast<btScalar>((width - 1) * scaleX * 0.5f),
                static_cast<btScalar>((minHeight + maxHeight) * scaleY * 0.5f),
                static_cast<btScalar>((depth - 1) * scaleZ * 0.5f)));
            compoundShape->addChildShape(childTransform, shape.get());

            shapeData.ownedChildShapes.push_back(std::move(shape));
            shapeData.shape = std::move(compoundShape);
            return shapeData;
        }

        // The render mesh a mesh collider takes its triangles from: a spline's generated collision mesh, otherwise
        // the entity's mesh.
        const render::Mesh *FindCollisionMesh(const Entity &entity)
        {
            if (const auto *spline = entity.GetComponent<SplineComponent>())
                return spline->GetGeneratedCollisionMesh();
            if (const auto *meshComponent = entity.GetComponent<MeshComponent>())
                return meshComponent->GetMesh();
            return nullptr;
        }

        std::uint64_t Bits(float value) noexcept
        {
            return std::bit_cast<std::uint32_t>(value);
        }

        struct SharedSource
        {
            BulletShapeCache::SourceKey key;
            std::weak_ptr<const void> lifetime;
        };

        // Which source a collider's shape is built from, for geometry worth sharing; nothing for cheap primitives.
        std::optional<SharedSource> DescribeSharedSource(const Entity &entity, const ColliderComponent &collider)
        {
            using Kind = BulletShapeCache::SourceKey::Kind;
            const glm::vec3 worldScale = entity.GetWorldScale();
            switch (collider.GetShape())
            {
            case ColliderShape::Terrain:
            {
                const auto *terrain = entity.GetComponent<TerrainComponent>();
                if (!terrain)
                    return std::nullopt;
                // A terrain belongs to one entity; entity IDs are never reused, so the ID names it for good.
                return SharedSource{
                    .key = {.kind = Kind::Terrain,
                            .identity = entity.GetID(),
                            .content = {terrain->GetHeightsRevision(), terrain->GetHolesRevision(),
                                        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(terrain->GetWidth())) << 32) |
                                            static_cast<std::uint32_t>(terrain->GetDepth()),
                                        Bits(terrain->GetCellSize())},
                            .worldScale = worldScale},
                };
            }
            case ColliderShape::Mesh:
            case ColliderShape::ConvexHull:
            {
                const auto *mesh = FindCollisionMesh(entity);
                auto lifetime = mesh ? mesh->GetLifetimeToken() : std::weak_ptr<const void>{};
                // Without a lifetime a freed mesh's address could come back as another mesh: build those afresh.
                if (!mesh || lifetime.expired())
                    return std::nullopt;
                return SharedSource{
                    .key = {.kind = collider.GetShape() == ColliderShape::ConvexHull ? Kind::ConvexHull : Kind::Mesh,
                            .identity = reinterpret_cast<std::uintptr_t>(mesh),
                            .content = {mesh->GetContentRevision()},
                            .worldScale = worldScale},
                    .lifetime = std::move(lifetime),
                };
            }
            default:
                return std::nullopt;
            }
        }

        // Whether two lifetime tokens belong to the same object. Two empty tokens (sources without one) match.
        bool SameSource(const std::weak_ptr<const void> &lhs, const std::weak_ptr<const void> &rhs) noexcept
        {
            return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
        }
    }

    BulletShapeData BuildColliderShape(const Entity &entity, const ColliderComponent &collider)
    {
        const glm::vec3 worldScale = entity.GetWorldScale();
        switch (collider.GetShape())
        {
        case ColliderShape::Terrain:
        {
            const auto *terrain = entity.GetComponent<TerrainComponent>();
            return terrain ? CreateTerrainShape(*terrain, worldScale) : BulletShapeData{};
        }
        case ColliderShape::Mesh:
        {
            const auto *mesh = FindCollisionMesh(entity);
            return mesh ? CreateMeshShape(*mesh, worldScale) : BulletShapeData{};
        }
        case ColliderShape::ConvexHull:
        {
            const auto *mesh = FindCollisionMesh(entity);
            return mesh ? CreateConvexHullShape(*mesh, worldScale) : BulletShapeData{};
        }
        default:
            return CreatePrimitiveColliderShape(collider, worldScale);
        }
    }

    std::size_t BulletShapeCache::SourceKeyHash::operator()(const SourceKey &key) const noexcept
    {
        std::size_t seed = static_cast<std::size_t>(key.kind);
        const auto combine = [&seed](std::uint64_t value)
        { seed ^= std::hash<std::uint64_t>{}(value) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2); };
        combine(key.identity);
        for (const auto value : key.content)
            combine(value);
        combine(Bits(key.worldScale.x));
        combine(Bits(key.worldScale.y));
        combine(Bits(key.worldScale.z));
        return seed;
    }

    SharedBulletShape BulletShapeCache::Acquire(const Entity &entity, const ColliderComponent &collider)
    {
        auto source = DescribeSharedSource(entity, collider);
        if (!source)
            return Build(entity, collider);

        if (const auto found = m_sharedShapes.find(source->key); found != m_sharedShapes.end())
        {
            if (SameSource(found->second.sourceLifetime, source->lifetime))
                if (auto shape = found->second.shape.lock())
                    return shape;
        }

        auto shape = Build(entity, collider);
        if (shape)
        {
            PruneExpiredEntries();
            m_sharedShapes.insert_or_assign(source->key, Entry{.sourceLifetime = std::move(source->lifetime), .shape = shape});
        }
        return shape;
    }

    std::size_t BulletShapeCache::GetSharedShapeCount() const
    {
        return static_cast<std::size_t>(std::count_if(m_sharedShapes.begin(), m_sharedShapes.end(),
                                                      [](const auto &entry) { return !entry.second.shape.expired(); }));
    }

    SharedBulletShape BulletShapeCache::Build(const Entity &entity, const ColliderComponent &collider)
    {
        auto data = BuildColliderShape(entity, collider);
        if (!data.shape)
            return nullptr;
        ++m_builtShapeCount;
        return std::make_shared<const BulletCollisionShape>(std::move(data));
    }

    void BulletShapeCache::PruneExpiredEntries()
    {
        std::erase_if(m_sharedShapes, [](const auto &entry) { return entry.second.shape.expired(); });
    }
}
