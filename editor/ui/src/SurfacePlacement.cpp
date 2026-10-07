#include "PlutoGE/ui/SurfacePlacement.h"
#include "PlutoGE/ui/PlacementGeometry.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/intersect.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <limits>

namespace PlutoGE::ui
{
    namespace
    {
        bool Finite(glm::vec3 value)
        { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

        void Visit(scene::Entity &entity, const std::function<void(scene::Entity &)> &visitor)
        {
            visitor(entity);
            for (auto *child : entity.GetChildren()) Visit(*child, visitor);
        }

        bool HitsBounds(const ViewportPickRay &ray, glm::vec3 low, glm::vec3 high)
        {
            float near = 0, far = std::numeric_limits<float>::max();
            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::abs(ray.direction[axis]) < 1e-12f)
                {
                    if (ray.origin[axis] < low[axis] || ray.origin[axis] > high[axis]) return false;
                    continue;
                }
                const float a = (low[axis] - ray.origin[axis]) / ray.direction[axis];
                const float b = (high[axis] - ray.origin[axis]) / ray.direction[axis];
                near = std::max(near, std::min(a, b));
                far = std::min(far, std::max(a, b));
                if (near > far) return false;
            }
            return true;
        }

        std::pair<std::size_t, std::size_t> SubmeshRange(const scene::MeshComponent &component)
        {
            const auto count = component.GetMesh()->GetSubmeshCount();
            const auto begin = component.GetSubmeshIndex() < 0 ? 0 :
                std::min(count, static_cast<std::size_t>(component.GetSubmeshIndex()));
            const auto end = component.GetSubmeshIndex() < 0 ? count :
                std::min(count, begin + static_cast<std::size_t>(component.GetSubmeshRangeCount()));
            return {begin, end};
        }

        glm::mat4 Matrix(const scene::Transform &transform)
        {
            const auto r = glm::radians(transform.rotation);
            return glm::translate(glm::mat4(1), transform.position) * glm::eulerAngleXYZ(r.x, r.y, r.z) *
                glm::scale(glm::mat4(1), transform.scale);
        }

        std::unique_ptr<scene::Scene> LoadPrototype(const std::string &reference, assets::AssetManager &assets,
                                                   std::string &error, const assets::Project *project, std::string *animationReference = nullptr)
        {
            if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Prefab)
                return scene::Prefab::LoadGeometryPreview(reference, &error);
            std::string meshReference = reference, bindingReference = reference;
            if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Model)
            {
                if (!project) { error = "Open a project before placing a model."; return nullptr; }
                if (!assets::ResolveModelPlacementMesh(*project, reference, meshReference, bindingReference, &error)) return nullptr;
            }
            if (assets::Project::GetAssetTypeForReference(meshReference) != assets::ProjectAssetType::Mesh)
            {
                error = "Surface placement accepts model, mesh and prefab assets.";
                return nullptr;
            }
            if (animationReference)
            {
                animationReference->clear();
                for (const auto &candidate : {meshReference, bindingReference})
                {
                    auto sibling = std::filesystem::path(assets.ResolveAssetPath(candidate));
                    sibling.replace_extension(".plutoanim");
                    std::error_code ec;
                    if (content::IsRegularFile(sibling, ec))
                    { *animationReference = assets.PersistAssetPath(sibling.string()); break; }
                }
            }
            auto *mesh = assets.LoadMeshAsset(meshReference);
            if (!mesh) { error = "Could not load mesh: " + reference; return nullptr; }
            auto prototype = std::make_unique<scene::Scene>();
            auto *root = prototype->AddEntity(std::make_unique<scene::Entity>(scene::EntityID{1}, scene::EntityConfig{
                .name = std::filesystem::path(reference).stem().string()}));
            auto *component = root->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = mesh});
            component->SetMeshAssetReference(meshReference);
            const auto &references = assets.GetMeshAssetMaterialReferences(bindingReference);
            for (std::size_t i = 0; i < references.size(); ++i)
            {
                if (references[i].empty()) continue;
                auto *material = assets.LoadMaterialAsset(references[i]);
                if (!material) { error = "Could not load material: " + references[i]; return nullptr; }
                component->SetMaterialForMaterialSlot(i, material);
                component->SetMaterialAssetForMaterialSlot(i, references[i]);
            }
            return prototype;
        }
    }

    std::optional<PlacementSurfaceHit> RaycastPlacementSurface(scene::Scene &scene,
                                                               const ViewportPickRay &input, float maxDistance)
    {
        if (!Finite(input.origin) || !Finite(input.direction) || !std::isfinite(glm::length(input.direction)) ||
            glm::length(input.direction) < 1e-8f ||
            !std::isfinite(maxDistance) || maxDistance <= 0) return std::nullopt;
        const ViewportPickRay ray{input.origin, glm::normalize(input.direction)};
        std::optional<PlacementSurfaceHit> best;
        float nearest = maxDistance;
        const auto accept = [&](glm::vec3 point, glm::vec3 normal, scene::EntityID id)
        {
            const float distance = glm::dot(point - ray.origin, ray.direction);
            if (!Finite(point) || !Finite(normal) || !std::isfinite(glm::length(normal)) || glm::length(normal) < 1e-8f ||
                distance < 0 || distance > nearest) return;
            nearest = distance;
            normal = glm::normalize(normal);
            if (glm::dot(normal, ray.direction) > 0) normal = -normal;
            best = PlacementSurfaceHit{point, normal, id};
        };
        scene.SynchronizePhysicsQueries();
        scene::PhysicsRaycastHit physical;
        if (scene.Raycast(ray.origin, ray.direction, maxDistance, physical))
            accept(physical.point, physical.normal, physical.entityId);
        for (auto *root : scene.GetRootEntities()) Visit(*root, [&](scene::Entity &entity)
        {
            if (!entity.IsActive()) return;
            if (auto *terrain = entity.GetComponent<scene::TerrainComponent>(); terrain && terrain->IsEnabled())
            {
                glm::vec3 point, normal;
                float height;
                if (terrain->Raycast(ray.origin, ray.direction, point))
                {
                    const auto inverse = glm::inverse(entity.GetWorldTransform());
                    const auto local = glm::vec3(inverse * glm::vec4(point, 1));
                    if (terrain->TrySampleSurface(local.x, local.z, height, normal))
                        accept(point, glm::transpose(glm::mat3(inverse)) * normal, entity.GetID());
                }
            }
            for (const auto *component : entity.GetComponents<scene::MeshComponent>())
            {
                auto *mesh = component->GetMesh();
                if (!mesh || !component->IsEnabled() || !component->IsVisible() || !IsRigidPlacementMesh(*mesh)) continue;
                const auto &data = mesh->GetMeshData();
                const auto [begin, end] = SubmeshRange(*component);
                for (auto index = begin; index < end; ++index)
                {
                    const auto &submesh = mesh->GetSubmesh(index);
                    glm::mat4 bind;
                    if (!TryGetPlacementBindTransform(*mesh, index, bind) || submesh.indexOffset > data.indices.size() ||
                        submesh.indexCount > data.indices.size() - submesh.indexOffset) continue;
                    const auto world = entity.GetWorldTransform() * component->GetMeshOffsetTransform() *
                        component->GetSubmeshOffsetTransform(index) * bind;
                    const auto localRay = TransformViewportPickRay(ray, world);
                    if (!localRay) continue;
                    if (submesh.hasBoundsExtents && !HitsBounds(*localRay, submesh.boundsMin, submesh.boundsMax)) continue;
                    for (std::size_t offset = 0; offset + 2 < submesh.indexCount; offset += 3)
                    {
                        glm::vec3 points[3];
                        bool valid = true;
                        for (int corner = 0; corner < 3; ++corner)
                        {
                            const auto vertexIndex = data.indices[submesh.indexOffset + offset + corner];
                            if (vertexIndex >= data.vertices.size()) { valid = false; break; }
                            const auto &v = data.vertices[vertexIndex].position;
                            points[corner] = {v[0], v[1], v[2]};
                        }
                        if (!valid) continue;
                        glm::vec2 barycentric;
                        float distance;
                        if (!glm::intersectRayTriangle(localRay->origin, localRay->direction,
                            points[0], points[1], points[2], barycentric, distance) || distance < 0) continue;
                        const auto localHit = localRay->origin + localRay->direction * distance;
                        accept(glm::vec3(world * glm::vec4(localHit, 1)),
                            glm::transpose(glm::inverse(glm::mat3(world))) *
                            glm::cross(points[1] - points[0], points[2] - points[0]), entity.GetID());
                    }
                }
            }
        });
        return best;
    }

    struct SurfacePlacementSession::State
    {
        std::unique_ptr<scene::Scene> prototype;
        scene::Entity *root = nullptr;
        assets::AssetManager *assets = nullptr;
        const assets::Project *project = nullptr;
        std::string reference, signature, animationReference;
        std::optional<scene::Transform> pose;
        scene::EntityID parentId = 0;
        glm::mat4 parentWorld{1};
        render::Material ghost{render::MaterialConfig{
            .color = {0.15f, 0.8f, 1.0f, 0.45f}, .alphaMode = render::AlphaMode::Blend,
            .castsShadow = false, .twoSided = true, .emission = {0.1f, 0.25f, 0.3f}}};
        std::vector<render::RenderCommand> commands;
    };

    SurfacePlacementSession::SurfacePlacementSession() : m_state(std::make_unique<State>()) {}
    SurfacePlacementSession::~SurfacePlacementSession() = default;
    void SurfacePlacementSession::Cancel() { m_state = std::make_unique<State>(); }
    bool SurfacePlacementSession::IsActive() const { return m_state->root != nullptr; }
    const std::string &SurfacePlacementSession::GetReference() const { return m_state->reference; }
    const scene::Entity *SurfacePlacementSession::GetPrototype() const { return m_state->root; }
    const std::optional<scene::Transform> &SurfacePlacementSession::GetPose() const { return m_state->pose; }
    const std::vector<render::RenderCommand> &SurfacePlacementSession::GetRenderCommands() const { return m_state->commands; }
    void SurfacePlacementSession::InvalidatePose() { m_state->pose.reset(); m_state->commands.clear(); }

    bool SurfacePlacementSession::Begin(std::string reference, assets::AssetManager &assets, std::string &error, const assets::Project *project)
    {
        Cancel();
        error.clear();
        auto prototype = LoadPrototype(reference, assets, error, project, &m_state->animationReference);
        if (!prototype || !error.empty()) return false;
        const auto roots = prototype->GetRootEntities();
        if (roots.size() != 1 || !roots.front()->IsSelfActive())
        { error = "Placement requires one active prefab root."; return false; }
        if (!std::isfinite(glm::determinant(roots.front()->GetWorldTransform())) ||
            std::abs(glm::determinant(roots.front()->GetWorldTransform())) < 1e-8f)
        { error = "Placement prototype has a singular transform."; return false; }
        std::size_t parts = 0;
        bool unsupported = false;
        Visit(*roots.front(), [&](scene::Entity &entity)
        {
            if (!entity.IsActive()) return;
            for (const auto *component : entity.GetComponents<scene::MeshComponent>())
            {
                const auto *mesh = component->GetMesh();
                if (!component->IsEnabled() || !component->IsVisible()) continue;
                if (!mesh) { unsupported = true; continue; }

                const auto [begin, end] = SubmeshRange(*component);
                parts += end - begin;
                for (auto i = begin; i < end; ++i)
                { glm::mat4 bind; unsupported |= !TryGetPlacementBindTransform(*mesh, i, bind); }
            }
        });
        if (unsupported || parts == 0 || parts > 4096)
        { error = "Placement needs 1–4096 visible mesh parts with valid bind nodes."; return false; }
        if (!scene::SceneSerializer::SaveToString(*prototype, m_state->signature, &error)) return false;
        m_state->root = roots.front();
        m_state->prototype = std::move(prototype);
        m_state->reference = std::move(reference);
        m_state->assets = &assets;
        m_state->project = project;
        return true;
    }

    bool SurfacePlacementSession::Update(const PlacementSurfaceHit &hit, const scene::Entity *parent,
                                         const SurfacePlacementOptions &options, std::string &error)
    {
        InvalidatePose();
        if (!IsActive()) { error = "No placement asset is selected."; return false; }
        scene::Transform pose;
        if (!GroundPlacement::ComputeAtSurface(*m_state->root, parent, hit.point, hit.normal, options, pose, error)) return false;
        m_state->parentId = parent ? parent->GetID() : 0;
        m_state->parentWorld = parent ? parent->GetWorldTransform() : glm::mat4(1);
        const auto rootInverse = glm::inverse(m_state->root->GetWorldTransform());
        const auto placedRoot = m_state->parentWorld * Matrix(pose);
        Visit(*m_state->root, [&](scene::Entity &entity)
        {
            if (!entity.IsActive()) return;
            for (const auto *component : entity.GetComponents<scene::MeshComponent>())
            {
                auto *mesh = component->GetMesh();
                if (!mesh || !component->IsEnabled() || !component->IsVisible()) continue;
                const auto [begin, end] = SubmeshRange(*component);
                for (auto i = begin; i < end; ++i)
                {
                    glm::mat4 bind;
                    if (!TryGetPlacementBindTransform(*mesh, i, bind)) continue;
                    render::RenderCommand command;
                    command.mesh = mesh;
                    command.material = &m_state->ghost;
                    command.model = placedRoot * rootInverse * entity.GetWorldTransform() *
                        component->GetMeshOffsetTransform() * component->GetSubmeshOffsetTransform(i) * bind;
                    command.previousModel = command.model;
                    command.submeshIndex = static_cast<std::uint32_t>(i);
                    command.castsShadow = false;
                    const auto &bounds = mesh->GetSubmesh(i).bounds;
                    command.worldBounds.center = glm::vec3(command.model * glm::vec4(bounds.center, 1));
                    command.worldBounds.radius = bounds.radius * std::max({glm::length(glm::vec3(command.model[0])),
                        glm::length(glm::vec3(command.model[1])), glm::length(glm::vec3(command.model[2]))});
                    command.previousWorldBounds = command.worldBounds;
                    m_state->commands.push_back(std::move(command));
                }
            }
        });
        m_state->pose = pose;
        return true;
    }

    glm::vec3 SurfacePlacementSession::GetPreviewSize() const
    {
        glm::vec3 low(std::numeric_limits<float>::infinity()), high(-std::numeric_limits<float>::infinity());
        for (const auto &command : m_state->commands)
        {
            const auto &part = command.mesh->GetSubmesh(command.submeshIndex);
            for (int corner = 0; corner < 8; ++corner)
            {
                const auto point = glm::vec3(command.model * glm::vec4(
                    (corner & 1) ? part.boundsMax.x : part.boundsMin.x,
                    (corner & 2) ? part.boundsMax.y : part.boundsMin.y,
                    (corner & 4) ? part.boundsMax.z : part.boundsMin.z, 1));
                low = glm::min(low, point); high = glm::max(high, point);
            }
        }
        return m_state->commands.empty() ? glm::vec3(0) : high - low;
    }

    scene::Entity *SurfacePlacementSession::Stamp(scene::Scene &destination, scene::Entity *parent, std::string &error)
    {
        error.clear();
        if (!IsActive() || !m_state->pose || (parent && !destination.ContainsEntity(parent)) ||
            m_state->parentId != (parent ? parent->GetID() : 0) ||
            m_state->parentWorld != (parent ? parent->GetWorldTransform() : glm::mat4(1)))
        { error = "Placement target changed; move the cursor to refresh the preview."; return nullptr; }
        // Revalidate geometry before any scene mutation. Asset changes must not
        // silently stamp a hierarchy different from the preview.
        auto latest = LoadPrototype(m_state->reference, *m_state->assets, error, m_state->project);
        std::string signature;
        if (!latest || !error.empty() || !scene::SceneSerializer::SaveToString(*latest, signature, &error)) return nullptr;
        if (signature != m_state->signature)
        { error = "The asset changed. Restart placement to refresh its preview."; InvalidatePose(); return nullptr; }
        if (!m_state->animationReference.empty())
        {
            std::vector<render::AnimationClip> clips;
            if (!m_state->assets->LoadAnimationAsset(m_state->animationReference, clips) || clips.empty())
            { error = "Could not load the model's animation asset."; return nullptr; }
        }
        scene::Entity *created = nullptr;
        if (assets::Project::GetAssetTypeForReference(m_state->reference) == assets::ProjectAssetType::Prefab)
            created = scene::Prefab::Instantiate(destination, m_state->reference, parent, &error);
        else
            created = scene::Prefab::DuplicateEntity(destination, *m_state->root, parent, false);
        if (!created) { if (error.empty()) error = "Could not instantiate the placement asset."; return nullptr; }
        if (!created->IsPrefabInstanceRoot() && !m_state->animationReference.empty())
        {
            auto *animation = created->CreateComponent<scene::AnimationComponent>();
            if (!animation->SetAnimationAssetReference(m_state->animationReference))
            {
                destination.RemoveEntity(created);
                error = "Could not attach the model's animation asset.";
                return nullptr;
            }
        }
        created->SetPosition(m_state->pose->position);
        created->SetRotation(m_state->pose->rotation);
        created->SetScale(m_state->pose->scale);
        if (created->IsPrefabInstanceRoot())
            for (const char *property : {"Transform.Position", "Transform.Rotation", "Transform.Scale"}) created->AddPrefabOverride(property);
        return created;
    }
}
