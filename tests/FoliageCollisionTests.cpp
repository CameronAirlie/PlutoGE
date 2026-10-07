#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <iostream>
#include <memory>
#include <limits>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include "PlutoGE/scene/components/TerrainComponent.h"

int main()
{
    using namespace PlutoGE::scene;

    Scene scene;
    auto storage = std::make_unique<Entity>(EntityConfig{.name = "Forest"});
    auto *owner = scene.AddEntity(std::move(storage));
    auto *foliage = owner->CreateComponent<FoliageComponent>();
    auto &type = foliage->AddType("Tree");
    type.asset.cellSize = 16.0f;
    type.asset.collisionEnabled = true;
    type.asset.collisionCenter = {0, 1, 0};
    type.asset.collisionRadius = 0.5f;
    type.asset.collisionHeight = 2.0f;
    type.instances.push_back(FoliageInstance{.id = 1});

    foliage->SetSelectedTypeIndex(static_cast<int>(foliage->GetTypeCount() - 1));
    if (foliage->GetInstances().size() != 1 || foliage->GetInstances().front().id == 0)
    {
        std::cerr << "Foliage instance did not retain its stable ID.\n";
        return 1;
    }
    if (foliage->BuildCollisionCells().size() != 1)
    {
        std::cerr << "Foliage collision was not partitioned into one cell.\n";
        return 1;
    }

    PhysicsRaycastHit hit;
    if (!scene.Raycast({0, 1, 5}, {0, 0, -1}, 10, hit) || hit.entityId != owner->GetID() || hit.foliageInstanceId != 1)
    {
        std::cerr << "Raycast did not hit the foliage capsule.\n";
        return 1;
    }

    owner->SetPosition({10, 0, 0});
    if (!scene.Raycast({10, 1, 5}, {0, 0, -1}, 10, hit) || hit.entityId != owner->GetID())
    {
        std::cerr << "Foliage collision cache did not follow its owner transform.\n";
        return 1;
    }

    owner->SetScale({10, 5, 10});
    type.instances.front().position = {1, 0, 0};
    type.instances.front().scale = {2, 2, 2};
    foliage->SetSelectedTypeInstanceTransform(0, {1, 0, 0}, {0, 0, 0}, {2, 2, 2});
    const auto &scaledOwnerCells = foliage->BuildCollisionCells();
    if (scaledOwnerCells.empty() || scaledOwnerCells.front().instances.empty())
    {
        std::cerr << "Foliage collision was not rebuilt after scaling its owner.\n";
        return 1;
    }
    const glm::mat4 &instanceTransform = scaledOwnerCells.front().instances.front().worldTransform;
    if (glm::distance(glm::vec3(instanceTransform[3]), glm::vec3(20, 0, 0)) > 0.001f ||
        std::abs(glm::length(glm::vec3(instanceTransform[0])) - 1.0f) > 0.001f)
    {
        std::cerr << "Foliage collision inherited render or owner scale.\n";
        return 1;
    }

    type.instances.front().position = {2.0f, -4.0f, 3.0f};
    type.instances.front().rotationDegrees = {1.0f, 2.0f, 3.0f};
    type.instances.front().scale = {0.5f, 0.75f, 1.0f};
    const std::uint64_t revisionBeforeSelectedSnap = foliage->GetRevision();
    const std::size_t selectedSnapCount = foliage->SnapSelectedTypeInstancesToSurface(
        [](float x, float z)
        {
            return x + z;
        });
    if (selectedSnapCount != 1 || type.instances.front().position != glm::vec3(2.0f, 5.0f, 3.0f) ||
        type.instances.front().rotationDegrees != glm::vec3(1.0f, 2.0f, 3.0f) ||
        type.instances.front().scale != glm::vec3(0.5f, 0.75f, 1.0f) ||
        foliage->GetRevision() == revisionBeforeSelectedSnap)
    {
        std::cerr << "Selected foliage type did not snap vertically to the sampled surface.\n";
        return 1;
    }

    const std::size_t treeTypeIndex = static_cast<std::size_t>(foliage->GetSelectedTypeIndex());
    auto &secondType = foliage->AddType("Bush");
    secondType.instances.push_back(FoliageInstance{.id = 2, .position = {-2.0f, 10.0f, 1.0f}});
    const std::size_t allSnapCount = foliage->SnapAllInstancesToSurface(
        [](float x, float z)
        {
            return x * z;
        });
    const auto *snappedTreeType = foliage->GetType(treeTypeIndex);
    const auto *snappedBushType = foliage->GetSelectedType();
    if (allSnapCount != 2 || !snappedTreeType || !snappedBushType ||
        snappedTreeType->instances.front().position.y != 6.0f || snappedBushType->instances.front().position.y != -2.0f)
    {
        std::cerr << "All foliage types did not snap to the sampled surface.\n";
        return 1;
    }
    // Paint candidates are independently sampled and rejected. Exercise this
    // without a graphics device using an uninitialized mesh prototype.
    PlutoGE::render::Mesh prototype(PlutoGE::render::MeshConfig{});
    // Earlier collision fixtures edited the public type arrays directly. Use
    // the normal restore path to normalize IDs before testing authored paint.
    foliage->RestoreInstanceSnapshot(foliage->CaptureInstanceSnapshot());
    foliage->SetMesh(&prototype);
    foliage->SetPaintEnabled(true);
    foliage->SetDensity(100);
    foliage->SetBrushRadius(5);
    const auto beforePaint = foliage->GetSelectedTypeInstanceCount();
    const auto rejectedRevision = foliage->GetRevision();
    if (foliage->ApplyBrushAtWorldPosition({10, 0, 0},
        [](float, float) -> std::optional<FoliageSurfaceSample> { return std::nullopt; }) ||
        foliage->GetRevision() != rejectedRevision)
    {
        std::cerr << "Rejected candidates modified foliage.\n";
        return 1;
    }
    foliage->SetTypeAlignToTerrainNormal(static_cast<std::size_t>(foliage->GetSelectedTypeIndex()), true);
    const glm::vec3 slope = glm::normalize(glm::vec3(-1, 1, -.25f));
    const glm::vec3 worldSlope = glm::normalize(glm::transpose(glm::inverse(glm::mat3(owner->GetWorldTransform()))) * slope);
    if (!foliage->ApplyBrushAtWorldPosition({10, 0, 0},
        [&](float x, float z) -> std::optional<FoliageSurfaceSample>
        {
            if (x < 0 || z < 0) return std::nullopt;
            return FoliageSurfaceSample{x + z * .25f, slope};
        }) || foliage->GetSelectedTypeInstanceCount() <= beforePaint)
    {
        std::cerr << "Valid surface candidates were not painted.\n";
        return 1;
    }
    const auto &painted = foliage->GetInstances();
    for (std::size_t i = beforePaint; i < painted.size(); ++i)
    {
        const auto &instance = painted[i];
        glm::mat4 tilt(1);
        tilt = glm::rotate(tilt, glm::radians(instance.rotationDegrees.y), glm::vec3(0, 1, 0));
        tilt = glm::rotate(tilt, glm::radians(instance.rotationDegrees.x), glm::vec3(1, 0, 0));
        tilt = glm::rotate(tilt, glm::radians(instance.rotationDegrees.z), glm::vec3(0, 0, 1));
        if (instance.position.x < 0 || instance.position.z < 0 || instance.id == 0 ||
            std::abs(instance.position.y - instance.position.x - instance.position.z * .25f) > .0001f ||
            glm::distance(glm::normalize(glm::mat3(owner->GetWorldTransform()) * glm::vec3(tilt[1])), worldSlope) > .0001f)
        {
            std::cerr << "Paint ignored bounds, sampled height, stable ID or slope alignment.\n";
            return 1;
        }
    }
    if (foliage->ApplyBrushAtWorldPosition({10, 0, 0},
        [](float, float) -> std::optional<FoliageSurfaceSample>
        { return FoliageSurfaceSample{std::numeric_limits<float>::quiet_NaN(), {0, 1, 0}}; }))
    {
        std::cerr << "Non-finite surface heights were accepted.\n";
        return 1;
    }
    const auto paintSnapshot = foliage->CaptureInstanceSnapshot();
    foliage->ClearSelectedTypeInstances();
    foliage->RestoreInstanceSnapshot(paintSnapshot);
    if (foliage->CaptureInstanceSnapshot() != paintSnapshot)
    {
        std::cerr << "Foliage history did not restore stable instance IDs and transforms.\n";
        return 1;
    }
    const auto serializedPaint = foliage->Serialize();
    FoliageComponent restoredFoliage;
    restoredFoliage.Deserialize(serializedPaint);
    if (!restoredFoliage.GetSelectedType()->alignToTerrainNormal ||
        restoredFoliage.GetType(treeTypeIndex)->alignToTerrainNormal)
    {
        std::cerr << "Growth orientation did not persist independently per type.\n";
        return 1;
    }
    const auto restoredPaint = restoredFoliage.CaptureInstanceSnapshot();
    if (restoredPaint.size() != paintSnapshot.size()) return 1;
    for (std::size_t t = 0; t < paintSnapshot.size(); ++t)
    {
        if (restoredPaint[t].size() != paintSnapshot[t].size()) return 1;
        for (std::size_t i = 0; i < paintSnapshot[t].size(); ++i)
            if (restoredPaint[t][i].id != paintSnapshot[t][i].id ||
                glm::distance(restoredPaint[t][i].position, paintSnapshot[t][i].position) > .0001f)
            {
                std::cerr << "Foliage serialization lost painted IDs or transforms.\n";
                return 1;
            }
    }
    // Missing orientation fields default to upright, without moving old data.
    auto legacyProperties = serializedPaint;
    std::erase_if(legacyProperties, [](const Property &property)
        { return property.name.ends_with(".AlignToTerrainNormal"); });
    restoredFoliage.Deserialize(legacyProperties);
    if (restoredFoliage.GetSelectedType()->alignToTerrainNormal ||
        restoredFoliage.CaptureInstanceSnapshot() != restoredPaint)
    {
        std::cerr << "Legacy orientation fallback changed authored instances.\n";
        return 1;
    }
    const auto existingInstances = foliage->CaptureInstanceSnapshot();
    foliage->SetTypeAlignToTerrainNormal(static_cast<std::size_t>(foliage->GetSelectedTypeIndex()), false);
    if (foliage->CaptureInstanceSnapshot() != existingInstances) return 1;
    owner->SetRotation({20, 35, -15});
    const auto uprightStart = foliage->GetSelectedTypeInstanceCount();
    if (!foliage->ApplyBrushAtWorldPosition(glm::vec3(owner->GetWorldTransform() * glm::vec4(1, 0, 1, 1)),
        [&](float x, float z) -> std::optional<FoliageSurfaceSample>
        { return FoliageSurfaceSample{x + z * .25f, slope}; })) return 1;
    for (std::size_t i = uprightStart; i < foliage->GetInstances().size(); ++i)
    {
        const auto &instance = foliage->GetInstances()[i];
        glm::mat4 tilt(1);
        tilt = glm::rotate(tilt, glm::radians(instance.rotationDegrees.y), glm::vec3(0, 1, 0));
        tilt = glm::rotate(tilt, glm::radians(instance.rotationDegrees.x), glm::vec3(1, 0, 0));
        tilt = glm::rotate(tilt, glm::radians(instance.rotationDegrees.z), glm::vec3(0, 0, 1));
        const glm::vec3 worldUp = glm::normalize(glm::mat3(owner->GetWorldTransform()) * glm::vec3(tilt[1]));
        if (glm::distance(worldUp, glm::vec3(0, 1, 0)) > .0001f)
        {
            std::cerr << "Upright foliage leaned with rotated/scaled terrain.\n";
            return 1;
        }
    }
    auto *terrain = owner->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 3, .depth = 3});
    terrain->Deserialize({{"HeightSamples", PropertyType::String, "0,1,2,1,2,3,2,3,4"}});
    float height;
    glm::vec3 normal;
    if (terrain->TrySampleSurface(-.01f, 0, height, normal) ||
        terrain->TrySampleSurface(2.01f, 0, height, normal) ||
        terrain->TrySampleSurface(0, std::numeric_limits<float>::infinity(), height, normal) ||
        !terrain->TrySampleSurface(1, 1, height, normal) || std::abs(height - 2) > .0001f ||
        glm::distance(normal, glm::normalize(glm::vec3(-1, 1, -1))) > .0001f ||
        !terrain->TrySampleSurface(0, 0, height, normal) || std::abs(height) > .0001f ||
        glm::distance(normal, glm::normalize(glm::vec3(-1, 1, -1))) > .0001f)
    {
        std::cerr << "Terrain surface bounds query failed.\n";
        return 1;
    }
    return 0;
}
