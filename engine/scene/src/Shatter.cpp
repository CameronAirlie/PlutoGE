#include "PlutoGE/scene/Shatter.h"

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/procedural/MeshFracture.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>

namespace PlutoGE::scene
{
    namespace
    {
        // splitmix64, as MeshFracture uses: the same seed gives the same velocities on every platform.
        class Random
        {
        public:
            explicit Random(std::uint32_t seed) : m_state(0xD1B54A32D192ED03ull ^ (static_cast<std::uint64_t>(seed) * 0x9E3779B97F4A7C15ull)) {}

            float Unit()
            {
                std::uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
                z ^= z >> 31;
                return static_cast<float>(static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0));
            }

            // Uniform in the unit ball.
            glm::vec3 InBall()
            {
                for (int attempt = 0; attempt < 32; ++attempt)
                {
                    const glm::vec3 value(Unit() * 2.0f - 1.0f, Unit() * 2.0f - 1.0f, Unit() * 2.0f - 1.0f);
                    if (glm::dot(value, value) <= 1.0f)
                        return value;
                }
                return glm::vec3(0.0f);
            }

        private:
            std::uint64_t m_state;
        };

        // One mesh to break, in its frame: the mesh's world rotation about its world origin, with everything else in
        // its world transform (scale, shear, mirroring, submesh offsets) baked into the vertices. Pieces keep that
        // frame, so they draw exactly where the source did with a unit-scale entity.
        struct SourceMesh
        {
            MeshComponent *component = nullptr;
            std::string name;
            glm::mat3 rotation{1.0f};
            glm::vec3 origin{0.0f};
            render::MeshData geometry;
            std::vector<render::Submesh> submeshes;
            // Piece material slot -> the source's material (and its asset, when it has one).
            std::vector<render::Material *> materials;
            std::vector<std::string> materialAssets;
            glm::vec3 boundsMin{0.0f};
            glm::vec3 boundsMax{0.0f};
        };

        bool OrthonormalFrame(const glm::mat4 &world, glm::mat3 &rotation)
        {
            const glm::vec3 x(world[0]);
            const glm::vec3 y(world[1]);
            if (glm::dot(x, x) < 1e-12f)
                return false;
            const glm::vec3 axisX = glm::normalize(x);
            const glm::vec3 orthogonalY = y - axisX * glm::dot(y, axisX);
            if (glm::dot(orthogonalY, orthogonalY) < 1e-12f)
                return false;
            const glm::vec3 axisY = glm::normalize(orthogonalY);
            rotation = glm::mat3(axisX, axisY, glm::cross(axisX, axisY));
            return true;
        }

        // The drawn submeshes of a mesh component in its frame, as the renderer places them (MeshComponent::
        // SubmitRenderCommands): entity world * mesh offset * submesh offset * imported node. Skinned meshes are
        // taken in their bind pose.
        bool CollectSourceMesh(MeshComponent &component, SourceMesh &source)
        {
            auto *mesh = component.GetMesh();
            auto *owner = component.GetOwner();
            if (!mesh || !owner)
                return false;
            const auto &data = mesh->GetMeshData();
            if (data.indices.empty() || data.vertices.empty())
                return false;

            const glm::mat4 meshWorld = owner->GetWorldTransform() * component.GetMeshOffsetTransform();
            if (!OrthonormalFrame(meshWorld, source.rotation))
                return false;
            source.origin = glm::vec3(meshWorld[3]);
            source.component = &component;
            source.name = owner->GetName();
            const glm::mat4 toFrame = glm::mat4(glm::transpose(source.rotation)) * glm::translate(glm::mat4(1.0f), -source.origin);

            const std::size_t submeshCount = std::max<std::size_t>(mesh->GetSubmeshCount(), 1);
            const int firstSubmesh = component.GetSubmeshIndex();
            const std::size_t begin = firstSubmesh >= 0 ? static_cast<std::size_t>(firstSubmesh) : 0;
            const std::size_t end = firstSubmesh >= 0
                                        ? std::min(begin + static_cast<std::size_t>(std::max(1, component.GetSubmeshRangeCount())), submeshCount)
                                        : submeshCount;
            std::unordered_map<render::Material *, std::uint32_t> slotOf;
            std::vector<int> remap(data.vertices.size(), -1);
            std::vector<unsigned int> touched;
            source.boundsMin = glm::vec3(std::numeric_limits<float>::max());
            source.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
            for (std::size_t submeshIndex = begin; submeshIndex < end; ++submeshIndex)
            {
                auto *material = component.GetMaterialForSubmesh(submeshIndex);
                if (!material)
                    continue; // Not drawn.
                std::size_t indexOffset = 0;
                std::size_t indexCount = data.indices.size();
                if (submeshIndex < mesh->GetSubmeshCount())
                {
                    const auto &submesh = mesh->GetSubmesh(submeshIndex);
                    indexOffset = submesh.indexOffset;
                    indexCount = submesh.indexCount;
                }
                indexCount = std::min(indexCount, data.indices.size() - std::min(indexOffset, data.indices.size()));
                if (indexCount < 3)
                    continue;

                const auto [slot, added] = slotOf.try_emplace(material, static_cast<std::uint32_t>(source.materials.size()));
                if (added)
                {
                    source.materials.push_back(material);
                    std::string asset = component.GetMaterialAssetForSubmesh(submeshIndex);
                    if (asset.empty() && !component.HasMaterialOverrideForSubmesh(submeshIndex) && submeshIndex < mesh->GetSubmeshCount())
                        asset = component.GetMaterialAssetForMaterialSlot(mesh->GetSubmesh(submeshIndex).materialIndex);
                    source.materialAssets.push_back(std::move(asset));
                }

                const glm::mat4 transform = toFrame * meshWorld * component.GetSubmeshOffsetTransform(submeshIndex) *
                                            component.GetSubmeshNodeTransform(submeshIndex);
                const glm::mat3 linear(transform);
                const float determinant = glm::determinant(linear);
                if (!(std::abs(determinant) > 1e-12f))
                    continue;
                const glm::mat3 normalMatrix = glm::transpose(glm::inverse(linear));
                const float handedness = determinant < 0.0f ? -1.0f : 1.0f;

                // Submeshes can share vertices but not transforms: each takes its own copies.
                for (const auto index : touched)
                    remap[index] = -1;
                touched.clear();
                const auto firstIndex = static_cast<std::uint32_t>(source.geometry.indices.size());
                for (std::size_t corner = 0; corner + 2 < indexCount; corner += 3)
                {
                    std::array<unsigned int, 3> triangle{};
                    bool valid = true;
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        const auto index = data.indices[indexOffset + corner + k];
                        if (index >= data.vertices.size())
                        {
                            valid = false;
                            break;
                        }
                        if (remap[index] < 0)
                        {
                            auto vertex = data.vertices[index];
                            const glm::vec3 position(transform * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1.0f));
                            glm::vec3 normal = normalMatrix * glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
                            glm::vec3 tangent = linear * glm::vec3(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]);
                            normal = glm::dot(normal, normal) > 1e-20f ? glm::normalize(normal) : normal;
                            tangent = glm::dot(tangent, tangent) > 1e-20f ? glm::normalize(tangent) : tangent;
                            vertex.position = {position.x, position.y, position.z};
                            vertex.normal = {normal.x, normal.y, normal.z};
                            vertex.tangent = {tangent.x, tangent.y, tangent.z, vertex.tangent[3] * handedness};
                            source.boundsMin = glm::min(source.boundsMin, position);
                            source.boundsMax = glm::max(source.boundsMax, position);
                            remap[index] = static_cast<int>(source.geometry.vertices.size());
                            touched.push_back(index);
                            source.geometry.vertices.push_back(vertex);
                        }
                        triangle[k] = static_cast<unsigned int>(remap[index]);
                    }
                    if (!valid)
                        continue;
                    // A mirroring transform turns the winding inside out; swap two corners to keep faces outward.
                    if (determinant < 0.0f)
                        std::swap(triangle[1], triangle[2]);
                    source.geometry.indices.insert(source.geometry.indices.end(), triangle.begin(), triangle.end());
                }
                const auto count = static_cast<std::uint32_t>(source.geometry.indices.size()) - firstIndex;
                if (count > 0)
                    source.submeshes.push_back(render::Submesh{.indexOffset = firstIndex, .indexCount = count, .materialIndex = slot->second});
            }
            return !source.submeshes.empty();
        }

        void CollectSubtree(Entity &entity, std::vector<SourceMesh> &sources)
        {
            if (!entity.IsActive())
                return;
            for (auto *component : entity.GetComponents<MeshComponent>())
            {
                if (!component->IsEnabled() || !component->IsVisible())
                    continue;
                SourceMesh source;
                if (CollectSourceMesh(*component, source))
                    sources.push_back(std::move(source));
            }
            for (auto *child : entity.GetChildren())
                CollectSubtree(*child, sources);
        }

        float BoxVolume(const glm::vec3 &minimum, const glm::vec3 &maximum)
        {
            const glm::vec3 size = glm::max(maximum - minimum, glm::vec3(0.0f));
            return size.x * size.y * size.z;
        }
    }

    std::vector<Entity *> Shatter(Scene &scene, Entity &root, const ShatterSettings &settings)
    {
        std::vector<SourceMesh> sources;
        CollectSubtree(root, sources);
        if (sources.empty())
            return {};

        // The burst is outward from the middle of everything that breaks, so a rock made of several meshes
        // flies apart as one.
        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());
        float largestVolume = 0.0f;
        for (const auto &source : sources)
        {
            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 local((corner & 1) ? source.boundsMax.x : source.boundsMin.x,
                                      (corner & 2) ? source.boundsMax.y : source.boundsMin.y,
                                      (corner & 4) ? source.boundsMax.z : source.boundsMin.z);
                const glm::vec3 world = source.origin + source.rotation * local;
                worldMin = glm::min(worldMin, world);
                worldMax = glm::max(worldMax, world);
            }
            largestVolume = std::max(largestVolume, BoxVolume(source.boundsMin, source.boundsMax));
        }
        const glm::vec3 burstCentre = (worldMin + worldMax) * 0.5f;
        const glm::vec3 impactDirection = glm::dot(settings.impactDirection, settings.impactDirection) > 1e-12f &&
                                                  std::isfinite(settings.impactDirection.x + settings.impactDirection.y + settings.impactDirection.z)
                                              ? glm::normalize(settings.impactDirection)
                                              : glm::vec3(0.0f);
        const float density = std::isfinite(settings.density) ? std::max(settings.density, 0.0f) : 0.0f;
        const float spin = std::isfinite(settings.spin) ? std::max(settings.spin, 0.0f) : 0.0f;
        const float burstSpeed = std::isfinite(settings.burstSpeed) ? settings.burstSpeed : 0.0f;
        const float impactSpeed = std::isfinite(settings.impactSpeed) ? settings.impactSpeed : 0.0f;
        const float jitter = 0.15f * (std::abs(burstSpeed) + std::abs(impactSpeed));

        Random random(settings.seed);
        std::vector<Entity *> spawned;
        for (std::size_t sourceIndex = 0; sourceIndex < sources.size() && spawned.size() < kMaxShatterPieces; ++sourceIndex)
        {
            auto &source = sources[sourceIndex];
            const float share = largestVolume > 0.0f ? BoxVolume(source.boundsMin, source.boundsMax) / largestVolume : 1.0f;
            FractureSettings fracture{
                .pieceCount = std::max(1, static_cast<int>(std::lround(static_cast<float>(std::max(settings.pieces, 1)) * share))),
                .seed = settings.seed + static_cast<std::uint32_t>(sourceIndex) * 7919u,
                .impactBias = settings.impactBias,
                .minPieceSize = std::isfinite(settings.minPieceSize) ? settings.minPieceSize : 0.0f,
            };
            if (settings.impactPoint)
                fracture.impactPoint = glm::transpose(source.rotation) * (*settings.impactPoint - source.origin);
            auto pieces = FractureMesh(source.geometry, source.submeshes, fracture);

            const glm::mat4 rotation(source.rotation);
            float rotationX = 0.0f, rotationY = 0.0f, rotationZ = 0.0f;
            glm::extractEulerAngleXYZ(rotation, rotationX, rotationY, rotationZ);
            int pieceNumber = 0;
            for (auto &piece : pieces)
            {
                if (spawned.size() >= kMaxShatterPieces)
                    break;
                const glm::vec3 position = source.origin + source.rotation * piece.centroid;
                auto entity = std::make_unique<Entity>(EntityConfig{.name = source.name + " piece " + std::to_string(++pieceNumber)});
                if (!settings.tag.empty())
                    entity->AddTag(settings.tag);
                entity->SetLocalTransform(Transform{
                    .position = position,
                    .rotation = glm::degrees(glm::vec3(rotationX, rotationY, rotationZ)),
                    .scale = glm::vec3(1.0f),
                });

                auto *mesh = entity->CreateComponent<MeshComponent>(MeshComponentConfig{});
                mesh->SetMaterials(source.materials);
                for (std::size_t slot = 0; slot < source.materialAssets.size(); ++slot)
                    if (!source.materialAssets[slot].empty())
                        mesh->SetMaterialAssetForMaterialSlot(slot, source.materialAssets[slot]);
                mesh->ShareMaterialOwnership(*source.component);
                mesh->SetMinShadowLod(source.component->GetMinShadowLod());
                mesh->SetStatic(false);
                if (!mesh->SetRuntimeGeometry(std::move(piece.mesh), std::move(piece.submeshes)))
                    continue;

                entity->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::ConvexHull});
                glm::vec3 outward = position - burstCentre;
                outward = glm::dot(outward, outward) > 1e-10f ? glm::normalize(outward) : glm::vec3(0.0f, 1.0f, 0.0f);
                glm::vec3 axis = random.InBall();
                axis = glm::dot(axis, axis) > 1e-10f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
                entity->CreateComponent<RigidbodyComponent>(RigidbodyComponentConfig{
                    .mass = std::max(piece.volume * density, kMinShatterPieceMass),
                    .velocity = outward * burstSpeed + impactDirection * impactSpeed + random.InBall() * jitter,
                    .angularVelocity = axis * (spin * random.Unit()),
                });
                spawned.push_back(scene.AddEntity(std::move(entity)));
            }
        }
        return spawned;
    }
}
