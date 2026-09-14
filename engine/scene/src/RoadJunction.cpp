#include "PlutoGE/scene/RoadJunction.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_set>

namespace PlutoGE::scene
{
    bool BuildRoadJunction(const std::vector<std::array<glm::vec3, 2>> &mouths,
        float uvMetersPerTile, render::MeshConfig &output, std::string &error,
        const std::vector<std::array<glm::vec3, 2>> &bottomMouths)
    {
        error.clear();
        const auto fail = [&](const char *message) { error = message; return false; };
        if (mouths.size() < 2 || mouths.size() > 8 || !std::isfinite(uvMetersPerTile) || uvMetersPerTile < 0.01f)
            return fail("A junction requires 2–8 open road entrances and a positive UV scale.");
        std::vector<glm::vec3> points;
        for (const auto &mouth : mouths) for (const auto &point : mouth)
        {
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(point[axis]) || std::abs(point[axis]) > 1000000)
                    return fail("Junction coordinates must be finite and within one million local units.");
            points.push_back(point);
        }
        std::vector<std::size_t> sorted(points.size());
        std::iota(sorted.begin(), sorted.end(), 0);
        std::sort(sorted.begin(), sorted.end(), [&](auto a, auto b)
        { return points[a].x != points[b].x ? points[a].x < points[b].x : points[a].z < points[b].z; });
        const auto cross = [&](auto a, auto b, auto c)
        {
            const auto ab = glm::dvec3(points[b]) - glm::dvec3(points[a]);
            const auto ac = glm::dvec3(points[c]) - glm::dvec3(points[a]);
            return ab.x * ac.z - ab.z * ac.x;
        };
        std::vector<std::size_t> hull;
        for (auto index : sorted)
        {
            while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), index) <= 0) hull.pop_back();
            hull.push_back(index);
        }
        const auto lower = hull.size();
        for (std::size_t i = sorted.size() - 1; i-- > 0;)
        {
            while (hull.size() > lower && cross(hull[hull.size() - 2], hull.back(), sorted[i]) <= 0) hull.pop_back();
            hull.push_back(sorted[i]);
        }
        hull.pop_back();
        if (hull.size() != points.size()) return fail("Road entrances overlap, are collinear, or lie inside the junction boundary.");
        for (std::size_t mouth = 0; mouth < mouths.size(); ++mouth)
        {
            const auto a = std::find(hull.begin(), hull.end(), mouth * 2) - hull.begin();
            const auto b = std::find(hull.begin(), hull.end(), mouth * 2 + 1) - hull.begin();
            if ((a + 1) % hull.size() != static_cast<std::size_t>(b) && (b + 1) % hull.size() != static_cast<std::size_t>(a))
                return fail("Each road entrance must be a separate boundary edge.");
        }
        render::MeshConfig result;
        glm::vec3 center(0);
        for (const auto &point : points) center += point / static_cast<float>(points.size());
        const auto vertex = [&](const glm::vec3 &point)
        {
            result.data.vertices.push_back({{point.x, point.y, point.z}, {0, 0, 0},
                {point.x / uvMetersPerTile, point.z / uvMetersPerTile}, {1, 0, 0, 1}});
        };
        vertex(center);
        for (auto index : hull) vertex(points[index]);
        std::vector<glm::vec3> normals(result.data.vertices.size(), glm::vec3(0));
        for (std::size_t i = 0; i < hull.size(); ++i)
        {
            const auto next = (i + 1) % hull.size();
            const auto normal = glm::cross(points[hull[next]] - center, points[hull[i]] - center);
            if (normal.y <= 0.000001f) return fail("Junction has a degenerate surface triangle.");
            result.data.indices.insert(result.data.indices.end(), {0, static_cast<unsigned>(next + 1), static_cast<unsigned>(i + 1)});
            normals[0] += normal; normals[next + 1] += normal; normals[i + 1] += normal;
        }
        for (std::size_t i = 0; i < normals.size(); ++i)
        {
            const auto normal = glm::normalize(normals[i]);
            result.data.vertices[i].normal = {normal.x, normal.y, normal.z};
        }
        if (!bottomMouths.empty() && bottomMouths != mouths)
        {
            if (bottomMouths.size() != mouths.size()) return fail("Junction bottom entrances must match the top entrances.");
            std::vector<glm::vec3> bottom;
            glm::vec3 bottomCenter(0);
            for (const auto &mouth : bottomMouths) for (const auto &point : mouth)
            {
                for (int axis = 0; axis < 3; ++axis)
                    if (!std::isfinite(point[axis]) || std::abs(point[axis]) > 1000000)
                        return fail("Invalid junction bottom coordinates.");
                bottom.push_back(point);
                bottomCenter += point / static_cast<float>(points.size());
            }
            const auto bottomBase = static_cast<unsigned>(result.data.vertices.size());
            vertex(bottomCenter);
            for (auto index : hull) vertex(bottom[index]);
            std::vector<glm::vec3> bottomNormals(hull.size() + 1, glm::vec3(0));
            for (std::size_t i = 0; i < hull.size(); ++i)
            {
                const auto next = (i + 1) % hull.size();
                const auto normal = glm::cross(bottom[hull[i]] - bottomCenter, bottom[hull[next]] - bottomCenter);
                if (normal.y >= -0.000001f) return fail("Junction underside is folded or degenerate.");
                result.data.indices.insert(result.data.indices.end(), {bottomBase, bottomBase + static_cast<unsigned>(i + 1), bottomBase + static_cast<unsigned>(next + 1)});
                bottomNormals[0] += normal; bottomNormals[i + 1] += normal; bottomNormals[next + 1] += normal;
            }
            for (std::size_t i = 0; i < bottomNormals.size(); ++i)
            {
                const auto normal = glm::normalize(bottomNormals[i]);
                result.data.vertices[bottomBase + i].normal = {normal.x, normal.y, normal.z};
            }
            for (std::size_t i = 0; i < hull.size(); ++i)
            {
                const auto a = hull[i], b = hull[(i + 1) % hull.size()];
                // Entrances join the road's open cross-section; only exposed
                // boundary edges need walls. Give each wall its own normals.
                if (a / 2 == b / 2) continue;
                const std::array<glm::vec3, 4> wall{points[a], points[b], bottom[a], bottom[b]};
                const std::array<unsigned, 6> indices{0, 1, 2, 2, 1, 3};
                for (unsigned triangle = 0; triangle < 6; triangle += 3)
                {
                    const auto normal = glm::cross(wall[indices[triangle + 1]] - wall[indices[triangle]], wall[indices[triangle + 2]] - wall[indices[triangle]]);
                    if (glm::dot(normal, normal) < 0.000000000001f) continue;
                    const auto n = glm::normalize(normal);
                    for (unsigned corner = 0; corner < 3; ++corner)
                    {
                        const auto index = indices[triangle + corner];
                        vertex(wall[index]);
                        auto &v = result.data.vertices.back();
                        v.normal = {n.x, n.y, n.z};
                        v.uv = {index % 2 ? glm::length(points[b] - points[a]) / uvMetersPerTile : 0.0f,
                            index >= 2 ? glm::length(points[index % 2 ? b : a] - wall[index]) / uvMetersPerTile : 0.0f};
                        v.tangent = {0, 0, 0, 1};
                        result.data.indices.push_back(static_cast<unsigned>(result.data.vertices.size() - 1));
                    }
                }
            }
        }
        result.submeshes.push_back({.indexOffset = 0, .indexCount = static_cast<uint32_t>(result.data.indices.size()), .name = "Road junction"});
        output = std::move(result);
        return true;
    }

    Entity *BakeRoadJunction(Entity &owner, const std::vector<RoadJunctionEndpoint> &endpoints,
        assets::AssetManager &assets, const std::string &reference, std::string &error)
    {
        error.clear();
        auto *scene = owner.GetScene();
        auto *road = owner.GetComponent<SplineComponent>();
        if (!scene || scene->IsRuntimeStarted() || !road || endpoints.size() < 2 || endpoints.size() > 8)
        { error = "Select an authoring road and 2–8 entrances."; return nullptr; }
        const auto transform = owner.GetWorldTransform();
        if (std::abs(glm::determinant(transform)) < 0.000001f)
        { error = "Road transform is singular."; return nullptr; }
        const auto inverse = glm::inverse(transform);
        std::vector<std::array<glm::vec3, 2>> mouths, bottomMouths;
        std::unordered_set<std::uint32_t> used;
        for (const auto &endpoint : endpoints)
        {
            auto *entity = scene->FindEntityByID(endpoint.entity);
            auto *spline = entity ? entity->GetComponent<SplineComponent>() : nullptr;
            if (!spline || spline->IsClosed() || !used.insert(endpoint.entity).second)
            { error = "Choose distinct open roads for the junction."; return nullptr; }
            spline->Update(0);
            std::array<glm::vec3, 2> mouth, bottom;
            if (!spline->GetEndpointEdge(endpoint.atEnd, mouth) || !spline->GetEndpointEdge(endpoint.atEnd, bottom, true))
            { error = "All entrances require generated road meshes."; return nullptr; }
            for (unsigned side = 0; side < 2; ++side)
            {
                mouth[side] = glm::vec3(inverse * entity->GetWorldTransform() * glm::vec4(mouth[side], 1));
                bottom[side] = glm::vec3(inverse * entity->GetWorldTransform() * glm::vec4(bottom[side], 1));
            }
            mouths.push_back(mouth);
            bottomMouths.push_back(bottom);
        }
        render::MeshConfig config;
        if (!BuildRoadJunction(mouths, road->GetUvMetersPerTile(), config, error, bottomMouths)) return nullptr;
        auto *material = road->GetMaterial();
        std::string materialReference = road->GetMaterialAssetReference();
        if (auto *source = owner.GetComponent<MeshComponent>())
        {
            if (auto *visibleMaterial = source->GetMaterialForSubmesh(0))
            {
                material = visibleMaterial;
                materialReference = source->HasMaterialOverrideForSubmesh(0)
                    ? source->GetMaterialAssetForSubmesh(0)
                    : source->GetMaterialAssetForMaterialSlot(0);
            }
        }
        if (!material)
        {
            materialReference = std::string(assets::Project::kBuiltinDefaultShadedMaterialReference);
            material = assets.LoadMaterialAsset(materialReference);
        }
        if (!material) { error = "Could not load a junction material."; return nullptr; }
        if (!assets.SaveMeshAsset(reference, config, {materialReference}, &error)) return nullptr;
        auto *mesh = assets.LoadMeshAsset(reference);
        if (!mesh) { error = "Could not load the exported junction asset."; return nullptr; }
        auto *junction = scene->AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Road junction"}), &owner);
        auto *component = junction->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh = mesh, .material = material});
        component->SetMeshAssetReference(reference);
        component->SetMaterialAssetForMaterialSlot(0, materialReference);
        component->SetStatic(true);
        junction->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Mesh});
        return junction;
    }
}
