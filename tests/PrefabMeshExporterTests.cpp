#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/PrefabMeshExporter.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <cmath>
#include <iostream>
#include <memory>

namespace
{
    PlutoGE::render::Mesh MakeTriangle(float x)
    {
        PlutoGE::render::MeshConfig config;
        config.data.vertices = {
            {{x + 0, 0, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
            {{x + 1, 0, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
            {{x + 0, 1, 0}, {0, 0, 1}, {0, 1}, {1, 0, 0, 1}},
        };
        config.data.indices = {0, 1, 2};
        config.submeshes.push_back({.indexOffset = 0, .indexCount = 3, .name = "Part"});
        return PlutoGE::render::Mesh(config);
    }

    bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }
}

int main()
{
    using namespace PlutoGE;
    scene::Scene scene;
    auto rootStorage = std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Tree"});
    auto *root = scene.AddEntity(std::move(rootStorage));
    auto trunkMesh = MakeTriangle(0.0f);
    auto *trunk = root->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &trunkMesh});
    trunk->SetMaterialAssetForSubmesh(0, "project://Materials/Trunk.plutomaterial");

    auto leavesStorage = std::make_unique<scene::Entity>(scene::EntityConfig{.name = "Leaves"});
    auto *leavesEntity = scene.AddEntity(std::move(leavesStorage), root);
    leavesEntity->SetPosition({0, 2, 0});
    auto leavesMesh = MakeTriangle(2.0f);
    auto *leaves = leavesEntity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = &leavesMesh});
    leaves->SetMaterialAssetForSubmesh(0, "project://Materials/Leaves.plutomaterial");

    assets::AssetManager assetManager;
    scene::PrefabMeshExportData output;
    std::string error;
    if (!scene::BuildStaticMeshFromEntityHierarchy(*root, assetManager, output, &error))
    {
        std::cerr << error << '\n';
        return 1;
    }
    if (output.meshComponentCount != 2 || output.submeshCount != 2 ||
        output.mesh.data.vertices.size() != 6 || output.mesh.data.indices.size() != 6)
    {
        std::cerr << "Combined prefab mesh has incorrect geometry counts.\n";
        return 1;
    }
    if (output.materialReferences.size() != 2 ||
        output.materialReferences[0] != "project://Materials/Trunk.plutomaterial" ||
        output.materialReferences[1] != "project://Materials/Leaves.plutomaterial")
    {
        std::cerr << "Prefab material slots were not preserved.\n";
        return 1;
    }
    if (!Near(output.mesh.data.vertices[3].position[0], 2.0f) ||
        !Near(output.mesh.data.vertices[3].position[1], 2.0f))
    {
        std::cerr << "Child transform was not baked into exported vertices.\n";
        return 1;
    }
    return 0;
}
