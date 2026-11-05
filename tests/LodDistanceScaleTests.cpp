// LOD distance scale (docs/FOLIAGE.md): selection moves every switch in proportion, and the setting travels from a
// foliage type's scene properties and asset file to its render commands. The GPU paths are covered by
// GpuInstanceCullingChecks.h, ImpostorChecks.h and EditorFoliageChecks.h.
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/LodDistanceScale.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderSceneFrontend.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b) { return std::abs(a - b) <= 1.0e-4f; }

    // Two triangles at LOD0 and one at LOD1, which the mesh uses below 100 px.
    render::Mesh TwoLodMesh()
    {
        render::MeshConfig config;
        config.data.vertices = {{{-1, -1, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
                                {{1, -1, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
                                {{1, 1, 0}, {0, 0, 1}, {1, 1}, {1, 0, 0, 1}},
                                {{-1, 1, 0}, {0, 0, 1}, {0, 1}, {1, 0, 0, 1}}};
        config.data.indices = {0, 1, 2, 0, 2, 3, 0, 1, 2};
        render::Submesh submesh;
        submesh.indexCount = 6;
        submesh.bounds = {glm::vec3(0.0f), 1.5f};
        submesh.lods = {{0, 6, 0.0f, std::numeric_limits<float>::max()}, {6, 3, 8.0f, 100.0f}};
        config.submeshes = {submesh};
        return render::Mesh(config);
    }

    void TestSanitize()
    {
        Require(Near(render::SanitizeLodDistanceScale(2.5f), 2.5f), "A scale in range should be kept");
        Require(Near(render::SanitizeLodDistanceScale(0.0f), render::MinLodDistanceScale), "A scale should be raised to the minimum");
        Require(Near(render::SanitizeLodDistanceScale(1000.0f), render::MaxLodDistanceScale), "A scale should be lowered to the maximum");
        Require(Near(render::SanitizeLodDistanceScale(std::numeric_limits<float>::quiet_NaN()), render::DefaultLodDistanceScale),
                "A scale that is not finite should become the default");
    }

    void TestChainSelection()
    {
        render::BasicInstanceLodChain unscaled;
        unscaled.lods = {{0, 3000, 1e30f}, {3000, 300, 1e30f}, {3300, 30, 1e30f}};
        const std::vector<float> triangles{1000.0f, 100.0f, 10.0f};
        unscaled.ApplyTriangleDensity(triangles);
        unscaled.minScreenRadiusPixels = 4.0f;

        // Scaling by s makes an instance at s times the distance (1 / s of the radius) choose what it chose before: the
        // thresholds, the triangle budget, the crossfade bands and the covered range all move.
        for (const float scale : {0.5f, 2.0f, 3.0f})
        {
            auto scaled = unscaled;
            scaled.distanceScale = scale;
            for (float pixels = 1.0f; pixels < 300.0f; pixels *= 1.07f)
            {
                Require(scaled.Select(pixels / scale) == unscaled.Select(pixels),
                        "A distance scale should move Select's switches in proportion");
                const auto a = scaled.SelectBlend(pixels / scale), b = unscaled.SelectBlend(pixels);
                Require(a.count == b.count && a.lods == b.lods && a.codes == b.codes,
                        "A distance scale should move SelectBlend's switches and crossfades in proportion");
            }
        }
    }

    void TestClusterLod()
    {
        auto mesh = TwoLodMesh();
        const auto clusterLod = [&](float scale)
        {
            render::RenderCommand command;
            command.mesh = &mesh;
            command.worldBounds = {glm::vec3(0.0f, 0.0f, -100.0f), 10.0f};
            command.lodDistanceScale = scale;
            render::RenderSceneFrontend frontend;
            frontend.SubmitRenderCommand(command);
            const render::CameraData camera{.view = glm::mat4(1.0f),
                                            .projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f)};
            frontend.PrepareVisibleRenderCommands(camera, 720); // about 62 px: below the 100 px switch
            const auto visible = frontend.GetVisibleRenderCommandView();
            Require(visible.size() == 1, "The cluster should be visible");
            return visible[0].lodIndex;
        };
        Require(clusterLod(1.0f) == 1u, "An unscaled cluster should take its coarse LOD");
        Require(clusterLod(2.0f) == 0u, "A distance scale should keep the cluster's fine LOD farther away");
    }

    void TestFoliageType()
    {
        scene::Scene world;
        auto *owner = world.AddEntity(std::make_unique<scene::Entity>());
        auto *foliage = owner->CreateComponent<scene::FoliageComponent>();
        foliage->AddType("Tree");
        foliage->AddType("Grass");
        Require(Near(foliage->GetType(0)->asset.lodDistanceScale, 1.0f), "New types should keep the engine's LOD distances");
        foliage->SetTypeLodDistanceScale(0, 1000.0f);
        Require(Near(foliage->GetType(0)->asset.lodDistanceScale, render::MaxLodDistanceScale), "The setter should sanitize the scale");
        foliage->SetTypeLodDistanceScale(0, 2.0f);

        scene::FoliageComponent restored;
        restored.Deserialize(foliage->Serialize());
        Require(Near(restored.GetType(0)->asset.lodDistanceScale, 2.0f) && Near(restored.GetType(1)->asset.lodDistanceScale, 1.0f),
                "Each type's scale should survive scene serialization");

        // Scenes saved before the scale have no key; a hand-edited one is sanitized.
        auto properties = foliage->Serialize();
        auto legacy = properties;
        std::erase_if(legacy, [](const scene::Property &property) { return property.name.find(".LodDistanceScale") != std::string::npos; });
        scene::FoliageComponent old;
        old.Deserialize(legacy);
        Require(Near(old.GetType(0)->asset.lodDistanceScale, 1.0f), "Older scenes should keep the engine's LOD distances");
        for (auto &property : properties)
            if (property.name.find(".LodDistanceScale") != std::string::npos) property.value = "not a number";
        scene::FoliageComponent malformed;
        malformed.Deserialize(properties);
        Require(Near(malformed.GetType(0)->asset.lodDistanceScale, 1.0f), "A malformed scale should load as the default");

        // Commands carry their own type's scale, and changing it rebuilds them under a new identity, which is how the
        // renderer learns that a foliage cluster changed.
        auto mesh = TwoLodMesh();
        render::Material material;
        foliage->GetType(0)->instances.push_back(scene::FoliageInstance{.id = 1});
        foliage->GetType(1)->instances.push_back(scene::FoliageInstance{.id = 2});
        foliage->SetTypeMeshAndMaterials(0, &mesh, {&material}, {});
        foliage->SetTypeMeshAndMaterials(1, &mesh, {&material}, {});
        const auto &commands = foliage->GetRenderCommands();
        const auto count = [&](float scale)
        {
            return std::count_if(commands.begin(), commands.end(), [&](const auto &command) { return Near(command.lodDistanceScale, scale); });
        };
        Require(count(2.0f) == 1 && count(1.0f) == 1, "Commands should carry their type's scale");
        const auto identityBefore = commands.front().sourceObject;
        foliage->SetTypeLodDistanceScale(0, 3.0f);
        const auto &changed = foliage->GetRenderCommands();
        Require(changed.front().sourceObject != identityBefore, "Changing the scale should rebuild the commands");
    }

    void TestFoliageTypeAsset()
    {
        const auto directory = std::filesystem::temp_directory_path() / "PlutoGELodDistanceScaleTests";
        std::filesystem::create_directories(directory);
        const auto path = (directory / "Tree.plutofoliage").string();
        const auto load = [&]
        {
            scene::FoliageTypeAsset loaded;
            std::string error;
            Require(scene::LoadFoliageTypeAsset(path, loaded, &error), error);
            return loaded.lodDistanceScale;
        };

        scene::FoliageTypeAsset asset;
        asset.lodDistanceScale = 2.5f;
        std::string error;
        Require(scene::SaveFoliageTypeAsset(path, asset, &error), error);
        Require(Near(load(), 2.5f), "The scale should survive the foliage type asset");

        const auto write = [&](const std::string &text)
        {
            std::ofstream file(path, std::ios::trunc);
            file << text;
        };
        write("PLUTOFOLIAGE\t1\nCELL_SIZE\t16\n");
        Require(Near(load(), 1.0f), "Older foliage assets should keep the engine's LOD distances");
        write("PLUTOFOLIAGE\t1\nLOD_DISTANCE_SCALE\t1000\n");
        Require(Near(load(), render::MaxLodDistanceScale), "A hand-edited asset's scale should be sanitized");
        std::filesystem::remove_all(directory);
    }
}

int main()
try
{
    TestSanitize();
    TestChainSelection();
    TestClusterLod();
    TestFoliageType();
    TestFoliageTypeAsset();
    std::cout << "LOD distance scale tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
