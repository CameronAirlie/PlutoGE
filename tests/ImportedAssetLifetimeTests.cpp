#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() try
{
    using namespace PlutoGE;
    const auto require = [](bool value, const char *message) {
        if (!value) throw std::runtime_error(message);
    };
    const auto root = std::filesystem::temp_directory_path() /
        ("plutoge-import-lifetime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(root, error); }
    } cleanup{root};

    const float vertices[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    {
        std::ofstream binary(root / "triangle.bin", std::ios::binary);
        binary.write(reinterpret_cast<const char *>(vertices), sizeof(vertices));
    }
    const std::string gltf = R"({"asset":{"version":"2.0"},
        "buffers":[{"uri":"triangle.bin","byteLength":36}],
        "bufferViews":[{"buffer":0,"byteLength":36}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],
        "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.25,0.5,0.75,1]}}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
        "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
    auto &engine = core::Engine::GetInstance();
    scene::Scene scene;
    scene::Entity *rootEntity = nullptr;
    std::vector<core::ImportedRenderMeshAsset> borrowed;
    std::vector<std::string> paths;
    // Exceed both the old active and retired cache capacities (8+8 and 32+32).
    for (int i = 0; i < 80; ++i)
    {
        const auto path = root / (std::to_string(i) + ".gltf");
        { std::ofstream file(path); file << gltf; }
        paths.push_back(path.string());
        auto asset = engine.ImportMeshAsset(paths.back());
        require(asset.mesh && !asset.materials.empty(), "Fixture import failed");
        const auto warm = engine.GetMeshImporter().ImportMeshSourceAsset(paths.back());
        require(!warm.materials.empty() && warm.materials.front().color == glm::vec4(.25f, .5f, .75f, 1),
                "Cooked material cache changed RGBA channel order");
        borrowed.push_back(asset);
        auto *entity = scene.AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name = std::to_string(i)}), rootEntity);
        if (!rootEntity) rootEntity = entity;
        auto *mesh = entity->CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{.mesh = asset.mesh});
        mesh->SetMaterials(asset.materials);
        mesh->SetMeshAssetReference(paths.back());
    }
    for (size_t i = 0; i < paths.size(); ++i)
    {
        const auto again = engine.ImportMeshAsset(paths[i]);
        require(again.mesh == borrowed[i].mesh, "Live mesh was evicted");
        require(again.materials == borrowed[i].materials, "Live materials were evicted");
        require(borrowed[i].materials.front()->GetConfig().color.r == .25f, "Borrowed material changed");
    }

    // Reimport one asset repeatedly while old scene/prefab generations remain alive.
    const auto source = engine.GetMeshImporter().ImportMeshSourceAsset(paths.front());
    std::vector<core::ImportedRenderMeshAsset> generations;
    for (int i = 0; i < 80; ++i)
    {
        auto changed = source;
        changed.materials.front().color.r = static_cast<float>(i + 1);
        changed.animations.resize(1);
        changed.animations.front().name = std::to_string(i);
        engine.GetMeshImporter().FinalizeImportedMeshAsset(paths.front(), std::move(changed));
        generations.push_back(engine.ImportMeshAsset(paths.front()));
    }
    for (size_t i = 0; i < generations.size(); ++i)
    {
        require(generations[i].materials.front()->GetConfig().color.r == static_cast<float>(i + 1), "Retired material was freed");
        require(generations[i].mesh->GetSubmeshCount() == 1, "Retired mesh was freed");
        require(generations[i].animations->front().name == std::to_string(i), "Retired animation array moved or was freed");
    }
    // Exercise the serializer/copy path from the reported prefab-update stack.
    auto *original = scene.GetRootEntities().front();
    require(!original->GetComponent<scene::MeshComponent>()->Serialize().empty(), "Material serialization failed");
    require(scene::Prefab::DuplicateEntity(scene, *original, nullptr, false) != nullptr, "Prefab component copy failed");
    const auto prefabPath = root / "ManyModels.plutoprefab";
    require(scene::Prefab::SaveFromEntity(*original, prefabPath), "Prefab save failed");
    auto *instance = scene::Prefab::Instantiate(scene, prefabPath.string());
    require(instance && scene::Prefab::UpdateInstance(*instance), "Prefab update failed after cache pressure");
    require(borrowed.front().materials.front()->GetConfig().color.r == .25f, "Original scene material did not survive reimports");
    std::cout << "Imported materials, meshes and prefab serialization survive 80 assets and 80 reimports\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
