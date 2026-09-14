#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/RoadPlacement.h"
#include "PlutoGE/scene/RoadJunction.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/assets/AssetManager.h"
#include <chrono>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE::scene;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main()
{
    const auto prefix = std::filesystem::temp_directory_path() / ("PlutoGE-road-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto meshPath = prefix.string() + ".plutomesh";
    const auto prefabPath = prefix.string() + ".plutoprefab";
    const auto junctionPath = prefix.string() + ".junction.plutomesh";
    try
    {
        // Force allocator address reuse deterministically: renderer cache entries
        // for the first mesh must expire before the second occupies its address.
        alignas(PlutoGE::render::Mesh) std::byte storage[sizeof(PlutoGE::render::Mesh)];
        auto *first = std::construct_at(reinterpret_cast<PlutoGE::render::Mesh *>(storage), PlutoGE::render::MeshConfig{});
        const auto lifetime = first->GetLifetimeToken();
        Check(!lifetime.expired(), "Live mesh token expired");
        std::destroy_at(first);
        auto *second = std::construct_at(reinterpret_cast<PlutoGE::render::Mesh *>(storage), PlutoGE::render::MeshConfig{});
        Check(lifetime.expired() && !second->GetLifetimeToken().expired(), "Reused address retained stale GPU cache identity");
        std::destroy_at(second);
        SplineComponentConfig config;
        config.points = {{{0, 0, 0}, {0, 0, 30}}, {{0, 0, 20}, {0, 0, 30}}};
        config.closed = false;
        config.guardrailHeight = 1;
        SplineComponent road(config);
        road.Rebuild();
        Check(road.GetGeneratedMesh() && road.GetGeneratedCollisionMesh(), "Road generation failed");
        const auto visual = road.GetGeneratedMesh()->GetMeshData();
        const auto collision = road.GetGeneratedCollisionMesh()->GetMeshData();
        // Every underside normal must oppose the banked top normal. The walls
        // must have separate vertices, so their lighting cannot bleed underneath.
        const auto roadRows = visual.vertices.size() / 12;
        Check(roadRows >= 2 && visual.vertices.size() == roadRows * 12, "Road side faces still share surface vertices");
        for (std::size_t row = 0; row < roadRows; ++row)
            for (unsigned axis = 0; axis < 3; ++axis)
                Check(std::abs(visual.vertices[row * 4].normal[axis] + visual.vertices[row * 4 + 2].normal[axis]) < 0.0001f,
                    "Underside normal does not oppose banked surface");

        for (unsigned vertex = 0; vertex < 4; ++vertex)
            for (unsigned axis = 0; axis < 3; ++axis)
                Check(std::abs(visual.vertices[vertex].position[axis] - collision.vertices[vertex].position[axis]) < 0.0001f, "Collision lost road banking");
        Check(std::abs(collision.vertices[0].position[1] - collision.vertices[1].position[1]) > 1, "Bank was flattened");
        const auto samples = road.GetCollisionPathPoints().size();
        Check(collision.vertices.size() == samples * 8, "Guardrail vertices missing");
        Check(collision.indices.size() == (samples - 1) * 30, "Guardrail collision triangles missing");
        road.Rebuild();
        const auto &rebuilt = road.GetGeneratedCollisionMesh()->GetMeshData();
        Check(rebuilt.indices == collision.indices && rebuilt.vertices.size() == collision.vertices.size(), "Non-deterministic topology");
        for (std::size_t vertex = 0; vertex < rebuilt.vertices.size(); ++vertex)
            for (unsigned axis = 0; axis < 3; ++axis)
                Check(rebuilt.vertices[vertex].position[axis] == collision.vertices[vertex].position[axis], "Non-deterministic positions");
        SplineComponent restored;
        restored.Deserialize(road.Serialize());
        Check(restored.GetGuardrailHeight() == 1 && restored.GetGeneratedCollisionMesh()->GetMeshData().indices == collision.indices, "Road serialization lost guardrails");
        road.SetGuardrailHeight(0);
        road.Rebuild();
        Check(road.GetGeneratedCollisionMesh()->GetMeshData().vertices.size() == samples * 4, "Guardrail removal failed");
        // Curvature creates enough adaptive rows to exercise all render LODs.
        config.points = {{{0, 0, 0}, {}}, {{8, 2, 10}, {0, 0, 20}}, {{-8, 0, 20}, {}}, {{0, 0, 30}, {}}};
        config.lodCount = 4;
        config.samplesPerSegment = 64;
        SplineComponent curved(config);
        curved.Rebuild();
        const auto *mesh = curved.GetGeneratedMesh();
        Check(mesh->GetSubmeshLodCount(0) == 4, "Render LODs missing");
        for (std::size_t level = 1; level < 4; ++level)
        {
            const auto range = mesh->GetSubmeshLodRange(0, level);
            Check(range.indexCount < mesh->GetSubmeshLodRange(0, level - 1).indexCount, "LOD did not reduce triangles");
            Check(range.indexOffset + range.indexCount <= mesh->GetMeshData().indices.size(), "LOD range invalid");
            for (std::size_t i = range.indexOffset; i < range.indexOffset + range.indexCount; ++i)
                Check(mesh->GetMeshData().indices[i] < mesh->GetMeshData().vertices.size(), "LOD vertex out of range");
        }
        const auto lodIndices = mesh->GetMeshData().indices;
        curved.Rebuild();
        Check(curved.GetGeneratedMesh()->GetMeshData().indices == lodIndices, "LOD rebuild is not deterministic");
        restored.Deserialize(curved.Serialize());
        Check(restored.GetLodCount() == 4 && restored.GetGeneratedMesh()->GetMeshData().indices == lodIndices, "LOD settings not persisted");
        PlutoGE::assets::AssetManager assets;
        std::string error;
        Check(curved.ExportMeshAsset(assets, meshPath, &error), error.c_str());
        const auto *exported = assets.LoadMeshAsset(meshPath);
        Check(exported && exported->GetSubmeshLodCount(0) == 4 && exported->GetMeshData().indices == lodIndices, "Mesh asset roundtrip lost road LODs");
        curved.SetLodCount(1); curved.Rebuild();
        Check(curved.GetGeneratedMesh()->GetSubmeshLodCount(0) == 1, "Disable LOD failed");
        config.points = {{{0, 0, 0}, {}}, {{0, 0, 20}, {}}};
        SplineComponent straight(config);
        const auto placements = SampleRoadsidePlacements(straight, 5, 1, true);
        Check(placements.size() == 10, "Roadside spacing/count incorrect");
        for (std::size_t index = 0; index < placements.size(); ++index)
        {
            Check(std::abs(placements[index].position.z - float(index / 2) * 5) < 0.0001f, "Placement drifted from arc distance");
            Check(std::abs(std::abs(placements[index].position.x) - 5) < 0.0001f, "Placement edge offset incorrect");
        }
        Check(SampleRoadsidePlacements(straight, 0, 1, true).empty(), "Invalid spacing accepted");
        config.points.back().position.z = 10000;
        SplineComponent longRoad(config);
        Check(SampleRoadsidePlacements(longRoad, 0.1f, 1, true).empty(), "Placement budget exceeded");
        PlutoGE::render::Material roadMaterial({});
        Scene scene;
        auto *prop = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Roadside prop"}));
        Check(Prefab::SaveFromEntity(*prop, prefabPath, &error), error.c_str());
        auto *owner = scene.AddEntity(std::make_unique<Entity>());
        owner->CreateComponent<SplineComponent>()->Deserialize(straight.Serialize());
        std::string beforeBake, afterBake;
        Check(SceneSerializer::SaveToString(scene, beforeBake, &error), error.c_str());
        auto *group = BakeRoadsidePrefabs(*owner, prefabPath, 5, 1, true, error);
        Check(group && group->GetChildren().size() == 10 && group->GetParent() == owner, "Roadside prefab bake failed");
        Check(SceneSerializer::SaveToString(scene, afterBake, &error), error.c_str());
        auto undone = SceneSerializer::LoadFromString(beforeBake, &error);
        auto redone = SceneSerializer::LoadFromString(afterBake, &error);
        Check(undone && undone->FindEntityByID(owner->GetID())->GetChildren().empty(), "Roadside undo snapshot failed");
        Check(redone && redone->FindEntityByID(group->GetID())->GetChildren().size() == 10, "Roadside redo snapshot failed");
        Check(!BakeRoadsidePrefabs(*owner, prefabPath + ".missing", 5, 1, true, error) && owner->GetChildren().size() == 1, "Failed bake modified hierarchy");
        std::vector<std::array<glm::vec3, 2>> mouths = {
            {{{-2, 0, 4}, {2, 1, 4}}}, {{{4, 0, 2}, {4, 0, -2}}}, {{{-4, 0, -2}, {-4, 0, 2}}}};
        PlutoGE::render::MeshConfig junctionMesh;
        Check(BuildRoadJunction(mouths, 2, junctionMesh, error), error.c_str());
        Check(junctionMesh.data.vertices.size() == 7 && junctionMesh.data.indices.size() == 18, "Three-way junction triangulation failed");
        auto bottomMouths = mouths;
        for (auto &mouth : bottomMouths) for (auto &point : mouth) point.y -= 0.75f;
        PlutoGE::render::MeshConfig thickJunction;
        Check(BuildRoadJunction(mouths, 2, thickJunction, error, bottomMouths), error.c_str());
        Check(thickJunction.data.indices.size() == 54, "Junction underside or exposed walls missing");
        for (std::size_t i = 0; i < 7; ++i)
        {
            Check(std::abs(thickJunction.data.vertices[i].position[1] - thickJunction.data.vertices[i + 7].position[1] - 0.75f) < 0.0001f,
                "Junction thickness lost at banked entrance");
            Check(thickJunction.data.vertices[i + 7].normal[1] < 0, "Junction underside normal points up");
        }
        for (std::size_t i = 0; i < thickJunction.data.indices.size(); i += 3)
        {
            const auto &a = thickJunction.data.vertices[thickJunction.data.indices[i]];
            const auto &b = thickJunction.data.vertices[thickJunction.data.indices[i + 1]];
            const auto &c = thickJunction.data.vertices[thickJunction.data.indices[i + 2]];
            const auto position = [](const auto &v) { return glm::vec3(v.position[0], v.position[1], v.position[2]); };
            Check(glm::dot(glm::cross(position(b) - position(a), position(c) - position(a)), glm::vec3(a.normal[0], a.normal[1], a.normal[2])) > 0,
                "Junction winding disagrees with surface normal");
        }
        const auto junctionIndices = junctionMesh.data.indices;
        mouths.push_back(mouths.front());
        Check(!BuildRoadJunction(mouths, 2, junctionMesh, error) && junctionMesh.data.indices == junctionIndices, "Invalid junction changed output");
        auto *other = scene.AddEntity(std::make_unique<Entity>());
        other->CreateComponent<SplineComponent>()->Deserialize(straight.Serialize());
        other->SetPosition({0, 0, 25});
        other->GetComponent<SplineComponent>()->SetThickness(0.75f);
        auto *ownerRoad = owner->GetComponent<SplineComponent>();
        ownerRoad->Update(0);
        auto *ownerMesh = owner->GetComponent<MeshComponent>();
        ownerMesh->SetMaterial(&roadMaterial);
        ownerMesh->SetMaterialAssetForMaterialSlot(0, "engine://builtin/material/default-shaded");
        ownerMesh->SetSubmeshIndex(0);
        ownerRoad->Rebuild();
        Check(ownerMesh->GetMaterialForSubmesh(0) == &roadMaterial, "Road rebuild cleared Mesh inspector material");
        Check(ownerMesh->GetSubmeshIndex() == -1, "Road rebuild omitted generated segments");
        auto *junction = BakeRoadJunction(*owner, {{owner->GetID(), true}, {other->GetID(), false}}, assets, junctionPath, error);
        Check(junction && junction->GetParent() == owner, error.c_str());
        for (const auto &[entrance, atEnd] : std::array<std::pair<Entity *, bool>, 2>{{{owner, true}, {other, false}}})
        {
            std::array<glm::vec3, 2> bottomEdge;
            Check(entrance->GetComponent<SplineComponent>()->GetEndpointEdge(atEnd, bottomEdge, true), "Road bottom entrance missing");
            for (auto point : bottomEdge)
            {
                point = glm::vec3(glm::inverse(owner->GetWorldTransform()) * entrance->GetWorldTransform() * glm::vec4(point, 1));
                bool found = false;
                for (const auto &v : junction->GetComponent<MeshComponent>()->GetMesh()->GetMeshData().vertices)
                    found |= glm::length(point - glm::vec3(v.position[0], v.position[1], v.position[2])) < 0.0001f;
                Check(found, "Baked junction lost a road's exact bottom entrance");
            }
        }

        Check(junction->GetComponent<MeshComponent>()->GetMaterialForSubmesh(0) == &roadMaterial, "Junction did not inherit visible road material");
        Check(assets.GetMeshAssetMaterialReferences(junctionPath).front() == "engine://builtin/material/default-shaded", "Junction asset lost material reference");
        PhysicsRaycastHit junctionHit;
        scene.SynchronizePhysicsQueries();
        Check(scene.Raycast({0, 3, 22.5f}, {0, -1, 0}, 10, junctionHit) && junctionHit.entityId == junction->GetID(), "Junction collision missing");
        ownerMesh->SetMaterial(nullptr);
        ownerMesh->SetMaterialAssetForMaterialSlot(0, "");
        auto *defaultJunction = BakeRoadJunction(*owner, {{owner->GetID(), true}, {other->GetID(), false}}, assets, junctionPath, error);
        Check(defaultJunction && defaultJunction->GetComponent<MeshComponent>()->GetMaterialForSubmesh(0), "Unassigned road produced an invisible junction");
        SplineComponentConfig cachedConfig;
        cachedConfig.closed = false;
        for (int index = 0; index < 8; ++index)
            cachedConfig.points.push_back({{float(index % 2), 0, float(index * 10)}, {0, 0, float(index * 2)}});
        SplineComponent cached(cachedConfig);
        cached.Rebuild();
        Check(cached.GetLastRebuiltSegmentCount() == 7, "Initial segment cache incomplete");
        cached.Rebuild();
        Check(cached.GetLastRebuiltSegmentCount() == 0, "Unchanged geometry rebuilt");
        cached.SetPointPosition(3, {3, 1, 30}); cached.Rebuild();
        Check(cached.GetLastRebuiltSegmentCount() == 4, "Point edit rebuilt outside its Catmull-Rom neighbourhood");
        cachedConfig.points[3].position = {3, 1, 30};
        SplineComponent fresh(cachedConfig); fresh.Rebuild();
        Check(cached.GetGeneratedMesh()->GetMeshData().indices == fresh.GetGeneratedMesh()->GetMeshData().indices, "Local rebuild topology differs from full rebuild");
        const auto &cachedVertices = cached.GetGeneratedMesh()->GetMeshData().vertices;
        const auto &freshVertices = fresh.GetGeneratedMesh()->GetMeshData().vertices;
        Check(cachedVertices.size() == freshVertices.size(), "Local rebuild vertex count differs");
        for (std::size_t index = 0; index < cachedVertices.size(); ++index)
            Check(cachedVertices[index].position == freshVertices[index].position && cachedVertices[index].normal == freshVertices[index].normal &&
                cachedVertices[index].uv == freshVertices[index].uv, "Local rebuild geometry/UV differs from full rebuild");
        cached.SetClosed(true); cached.Rebuild();
        Check(cached.GetLastRebuiltSegmentCount() == 8, "Topology edit did not invalidate all segments");
        cached.SetPointRotation(0, {0, 0, 20}); cached.Rebuild();
        Check(cached.GetLastRebuiltSegmentCount() == 4, "Closed seam neighbourhood invalidation failed");
        const auto *closedMesh = cached.GetGeneratedMesh();
        const auto &closedData = closedMesh->GetMeshData();
        for (std::size_t index = 0; index < closedMesh->GetSubmeshCount(); ++index)
        {
            const auto &a = closedMesh->GetSubmesh(index);
            const auto &b = closedMesh->GetSubmesh((index + 1) % closedMesh->GetSubmeshCount());
            // This fixture has thickness and no rails: each base-detail edge
            // has 24 indices, with its top quad first. Follow that quad's
            // endpoint instead of assuming the largest index is a top vertex.
            const auto last = closedData.indices[a.indexOffset + a.indexCount - 24 + 2];
            const auto first = *std::min_element(closedData.indices.begin() + b.indexOffset, closedData.indices.begin() + b.indexOffset + b.indexCount);
            for (unsigned side = 0; side < 2; ++side) for (unsigned axis = 0; axis < 3; ++axis)
            {
                Check(std::abs(closedData.vertices[last + side].position[axis] - closedData.vertices[first + side].position[axis]) < 0.0001f, "Road segment position seam");
                Check(std::abs(closedData.vertices[last + side].normal[axis] - closedData.vertices[first + side].normal[axis]) < 0.0001f, "Road segment normal seam");
            }
        }
        cachedConfig.points.assign(3, SplineControlPoint{});
        SplineComponent degenerate(cachedConfig); degenerate.Rebuild();
        for (const auto &vertex : degenerate.GetGeneratedMesh()->GetMeshData().vertices)
            for (unsigned axis = 0; axis < 3; ++axis)
                Check(std::isfinite(vertex.position[axis]) && std::isfinite(vertex.normal[axis]), "Degenerate road produced nonfinite geometry");
        std::filesystem::remove(meshPath);
        std::filesystem::remove(prefabPath);
        std::filesystem::remove(junctionPath);
        std::cout << "Road banking, guardrail collision, determinism and serialization tests passed\n";
    }
    catch (const std::exception &e)
    {
        std::error_code ignored;
        std::filesystem::remove(meshPath, ignored); std::filesystem::remove(prefabPath, ignored);
        std::filesystem::remove(junctionPath, ignored);
        std::cerr << e.what() << '\n'; return 1;
    }
}
