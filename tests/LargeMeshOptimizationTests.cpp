#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/import/FoliageCards.h"
#include "PlutoGE/import/MeshTriangleBudget.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#undef assert
#define assert(condition)                                                                \
    do                                                                                   \
    {                                                                                    \
        if (!(condition))                                                                \
        {                                                                                \
            std::cerr << "Check failed at line " << __LINE__ << ": " #condition << '\n'; \
            return 1;                                                                    \
        }                                                                                \
    } while (false)

// Unlike assert, holds in Release builds too (NDEBUG compiles assert out).
static void Expect(bool condition, const char *message)
{
    if (condition) return;
    // An uncaught exception ends the process without a word on Windows: say what failed first.
    std::cerr << "FAILED: " << message << std::endl;
    throw std::runtime_error(message);
}

int main(int argc, char **argv)
{
    using namespace PlutoGE;

    // Degenerate source normals must not create NaN tangent space during UV repair.
    render::MeshConfig degenerate;
    degenerate.data.vertices = {
        {{0,0,0}, {0,0,0}, {0,0}, {}},
        {{1,0,0}, {0,0,0}, {1,0}, {}},
        {{0,1,0}, {0,0,0}, {0,1}, {}}};
    degenerate.data.indices = {0,1,2};
    render::Mesh repaired(degenerate);
    for (const auto &vertex : repaired.GetMeshData().vertices)
        for (const float value : vertex.tangent) assert(std::isfinite(value));

    // Optional read-only benchmark against an existing project and model identity.
    if (argc == 4)
    {
        assets::AssetManager manager;
        manager.SetProjectContext(argv[1]);
        const auto localId = std::stoull(argv[3]);
        for (const bool cold : {true, false})
        {
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 1532; ++i)
            {
                if (cold) manager.SetProjectContext(argv[1]);
                assert(!manager.ResolveModelObject(argv[2], localId).empty());
            }
            std::cout << (cold ? "Uncached" : "Cached") << " model resolution (1532 objects): "
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                      << " seconds" << std::endl;
        }
        return 0;
    }

    if (argc < 2 || std::string_view(argv[1]) != "--lod-only")
    {
        const auto resolutionRoot = std::filesystem::temp_directory_path() / "plutoge_model_resolution_test";
        const auto resolutionAssets = resolutionRoot / "Assets";
        std::filesystem::create_directories(resolutionAssets);
        std::ofstream(resolutionAssets / "Model.fbx.plutometa") << "ID\tmodel-id\n";
        assets::ModelAsset resolutionModel;
        resolutionModel.objects.push_back({.localId = 42, .reference = "project://first.plutomesh"});
        const auto manifest = resolutionAssets / "Model.plutomodel";
        assert(assets::SaveModelAsset(manifest.string(), resolutionModel));
        assets::AssetManager resolver;
        resolver.SetProjectContext(resolutionRoot.string());
        assert(resolver.GetStableAssetId("project://Model.fbx") == "model-id");
        const auto initialSource = resolver.ResolveStableAssetId("model-id");
        assert(initialSource == "project://Model.fbx");
        assert(resolver.ResolveModelObject("model-id", 42) == "project://first.plutomesh");
        assert(resolver.ResolveModelObject("model-id", 42) == "project://first.plutomesh");
        assert(resolver.ResolveModelObject("model-id", 99).empty());
        const auto previousStamp = std::filesystem::last_write_time(manifest);
        resolutionModel.objects[0].reference = "project://changed.plutomesh";
        assert(assets::SaveModelAsset(manifest.string(), resolutionModel));
        std::filesystem::last_write_time(manifest, previousStamp + std::chrono::seconds(2));
        assert(resolver.ResolveModelObject("model-id", 42) == "project://changed.plutomesh");
        std::filesystem::rename(resolutionAssets / "Model.fbx.plutometa", resolutionAssets / "Renamed.fbx.plutometa");
        std::filesystem::rename(manifest, resolutionAssets / "Renamed.plutomodel");
        assert(resolver.ResolveModelObject("model-id", 42) == "project://changed.plutomesh");
        resolver.ClearProjectContext();
        assert(resolver.ResolveModelObject("model-id", 42).empty());
        resolver.SetProjectContext(resolutionRoot.string());
        assert(resolver.ResolveModelObject("model-id", 42) == "project://changed.plutomesh");
        const auto renamedManifest = resolutionAssets / "Renamed.plutomodel";
        const auto legacyManifest = resolutionAssets / "Imported" / "Renamed" / "Renamed.plutomodel";
        std::filesystem::create_directories(legacyManifest.parent_path());
        std::filesystem::rename(renamedManifest, legacyManifest);
        assert(resolver.ResolveModelObject("model-id", 42) == "project://changed.plutomesh");
        resolutionModel.objects[0].reference = "project://canonical.plutomesh";
        assert(assets::SaveModelAsset(renamedManifest.string(), resolutionModel));
        assert(resolver.ResolveModelObject("model-id", 42) == "project://canonical.plutomesh");
        std::ofstream(renamedManifest, std::ios::trunc) << "invalid manifest";
        assert(resolver.ResolveModelObject("model-id", 42).empty());
        assert(assets::SaveModelAsset(renamedManifest.string(), resolutionModel));
        assert(resolver.ResolveModelObject("model-id", 42) == "project://canonical.plutomesh");
        assert(resolver.ResolveStableAssetId("missing-id", "fallback-a") == "fallback-a");
        assert(resolver.ResolveStableAssetId("missing-id", "fallback-b") == "fallback-b");
        std::filesystem::remove_all(resolutionRoot);

        scene::RectTransformComponent childRect;
        childRect.SetAnchorPreset(scene::UIAnchorPreset::MiddleCenter);
        childRect.SetSizeDelta(glm::vec2(100.0f, 40.0f));
        const auto childLayout = scene::ResolveRectTransformLayout(
            childRect, {.min = glm::vec2(100.0f, 100.0f), .max = glm::vec2(300.0f, 200.0f)});
        assert(childLayout.min == glm::vec2(150.0f, 130.0f));
        assert(childLayout.max == glm::vec2(250.0f, 170.0f));

        childRect.SetAnchorPreset(scene::UIAnchorPreset::Stretch);
        childRect.SetSizeDelta(glm::vec2(0.0f));
        const auto stretchedLayout = scene::ResolveRectTransformLayout(
            childRect, {.min = glm::vec2(100.0f, 100.0f), .max = glm::vec2(300.0f, 200.0f)});
        assert(stretchedLayout.min == glm::vec2(100.0f, 100.0f));
        assert(stretchedLayout.max == glm::vec2(300.0f, 200.0f));

        scene::ParticleSystemComponent oneShotParticles;
        oneShotParticles.SetLooping(false);
        oneShotParticles.SetDuration(0.1f);
        oneShotParticles.SetStartLifetime(1.0f);
        oneShotParticles.SetEmissionRateOverTime(0.0f);
        oneShotParticles.Emit(1);
        oneShotParticles.Update(0.2f);
        assert(!oneShotParticles.IsPlaying());
        assert(oneShotParticles.GetParticleCount() == 1);
        for (int step = 0; step < 5; ++step)
        {
            oneShotParticles.Update(0.2f);
        }
        assert(oneShotParticles.GetParticleCount() == 0);

        assetimport::MeshImportOptions options;
        assert(options.ToFlags() == 0);
        options.generateLods = true;
        options.optimizeVertexCache = true;
        options.optimizeOverdraw = true;
        assert(options.ToFlags() == 7);

        const auto meshAssetPath = std::filesystem::temp_directory_path() / "plutoge_large_mesh_metadata_test.plutomesh";
        assets::AssetManager assetManager;
        assets::MeshAssetMetadata metadata;
        metadata.sourceAssetReference = "source.glb";
        metadata.importOptions = options;
        render::MeshConfig emptyMeshConfig;
        std::string errorMessage;
        assert(assetManager.SaveMeshAsset(meshAssetPath.string(), emptyMeshConfig, {}, &errorMessage, metadata));
        const auto &loadedMetadata = assetManager.GetMeshAssetMetadata(meshAssetPath.string());
        assert(loadedMetadata.sourceAssetReference == metadata.sourceAssetReference);
        assert(loadedMetadata.importOptions.ToFlags() == metadata.importOptions.ToFlags());

        // Version 3 ends immediately after material references. Rewriting the
        // version and trimming the version-4 metadata verifies disabled defaults.
        std::ifstream version4Input(meshAssetPath, std::ios::binary);
        std::vector<char> version3Bytes((std::istreambuf_iterator<char>(version4Input)), std::istreambuf_iterator<char>());
        version4Input.close();
        assert(version3Bytes.size() > metadata.sourceAssetReference.size() + 15);
        const std::uint32_t version3 = 3;
        std::memcpy(version3Bytes.data() + sizeof(std::uint32_t), &version3, sizeof(version3));
        version3Bytes.resize(version3Bytes.size() - sizeof(std::uint64_t) - metadata.sourceAssetReference.size() - 3);
        std::ofstream version3Output(meshAssetPath, std::ios::binary | std::ios::trunc);
        version3Output.write(version3Bytes.data(), static_cast<std::streamsize>(version3Bytes.size()));
        version3Output.close();
        assets::AssetManager legacyAssetManager;
        const auto &legacyMetadata = legacyAssetManager.GetMeshAssetMetadata(meshAssetPath.string());
        assert(legacyMetadata.sourceAssetReference.empty());
        assert(legacyMetadata.importOptions.ToFlags() == 0);
        std::filesystem::remove(meshAssetPath);

    }
    const assetimport::MeshImportOptions options{true, true, true};

    const auto lodTestDirectory = std::filesystem::temp_directory_path() / "plutoge_lod_generation_test";
    std::filesystem::create_directories(lodTestDirectory);
    const auto gridBinaryPath = lodTestDirectory / "grid.bin";
    const auto gridGltfPath = lodTestDirectory / "grid.gltf";
    std::vector<float> positions;
    for (std::uint16_t y = 0; y < 5; ++y)
    {
        for (std::uint16_t x = 0; x < 5; ++x)
        {
            positions.insert(positions.end(), {static_cast<float>(x), static_cast<float>(y), 0.0f});
        }
    }
    std::vector<std::uint16_t> indices;
    for (std::uint16_t y = 0; y < 4; ++y)
    {
        for (std::uint16_t x = 0; x < 4; ++x)
        {
            const auto topLeft = static_cast<std::uint16_t>(y * 5 + x);
            indices.insert(indices.end(), {
                                              topLeft,
                                              static_cast<std::uint16_t>(topLeft + 1),
                                              static_cast<std::uint16_t>(topLeft + 5),
                                              static_cast<std::uint16_t>(topLeft + 1),
                                              static_cast<std::uint16_t>(topLeft + 6),
                                              static_cast<std::uint16_t>(topLeft + 5),
                                          });
        }
    }
    std::ofstream gridBinary(gridBinaryPath, std::ios::binary | std::ios::trunc);
    gridBinary.write(reinterpret_cast<const char *>(positions.data()), static_cast<std::streamsize>(positions.size() * sizeof(float)));
    gridBinary.write(reinterpret_cast<const char *>(indices.data()), static_cast<std::streamsize>(indices.size() * sizeof(std::uint16_t)));
    gridBinary.close();
    const auto positionBytes = positions.size() * sizeof(float);
    const auto indexBytes = indices.size() * sizeof(std::uint16_t);
    std::ofstream gridGltf(gridGltfPath, std::ios::trunc);
    gridGltf << "{\"asset\":{\"version\":\"2.0\"},"
             << "\"buffers\":[{\"uri\":\"grid.bin\",\"byteLength\":" << positionBytes + indexBytes << "}],"
             << "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" << positionBytes << ",\"target\":34962},"
             << "{\"buffer\":0,\"byteOffset\":" << positionBytes << ",\"byteLength\":" << indexBytes << ",\"target\":34963}],"
             << "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":25,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[4,4,0]},"
             << "{\"bufferView\":1,\"componentType\":5123,\"count\":" << indices.size() << ",\"type\":\"SCALAR\"}],"
             << "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
             << "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
    gridGltf.close();

    assetimport::MeshImporter importer;
    const auto generatedAsset = importer.ImportMeshSourceAsset(gridGltfPath.string(), options);
    assert(!generatedAsset.submeshes.empty());
    for (const auto &submesh : generatedAsset.submeshes)
    {
        assert(submesh.lods.size() > 1);
        std::uint32_t previousIndexCount = std::numeric_limits<std::uint32_t>::max();
        for (const auto &lod : submesh.lods)
        {
            assert(lod.indexCount % 3 == 0);
            assert(lod.indexOffset + lod.indexCount <= generatedAsset.meshData.indices.size());
            assert(lod.indexCount <= previousIndexCount);
            previousIndexCount = lod.indexCount;
        }
    }
    {
        // A canopy of leaf cards: thousands of disconnected quads, every vertex on a border. The standard profile locks
        // borders and so barely reduces it; the vegetation profile must take its coarsest LOD to a few percent.
        const auto leavesBinaryPath = lodTestDirectory / "leaves.bin";
        const auto leavesGltfPath = lodTestDirectory / "leaves.gltf";
        std::vector<float> leafPositions;
        std::vector<std::uint32_t> leafIndices;
        std::uint32_t seed = 12345u;
        const auto random = [&seed] { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 16777216.0f; };
        constexpr std::uint32_t leaves = 3000;
        for (std::uint32_t leaf = 0; leaf < leaves; ++leaf)
        {
            const float cx = random() * 6.0f - 3.0f, cy = random() * 4.0f + 2.0f, cz = random() * 6.0f - 3.0f;
            const float size = 0.05f + random() * 0.15f, angle = random() * 6.2831853f;
            const float ax = std::cos(angle) * size, az = std::sin(angle) * size;
            const auto base = static_cast<std::uint32_t>(leafPositions.size() / 3);
            leafPositions.insert(leafPositions.end(), {cx - ax, cy, cz - az, cx + ax, cy, cz + az,
                                                       cx + ax, cy + size, cz + az, cx - ax, cy + size, cz - az});
            leafIndices.insert(leafIndices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        std::ofstream leavesBinary(leavesBinaryPath, std::ios::binary | std::ios::trunc);
        leavesBinary.write(reinterpret_cast<const char *>(leafPositions.data()), static_cast<std::streamsize>(leafPositions.size() * sizeof(float)));
        leavesBinary.write(reinterpret_cast<const char *>(leafIndices.data()), static_cast<std::streamsize>(leafIndices.size() * sizeof(std::uint32_t)));
        leavesBinary.close();
        const auto leafPositionBytes = leafPositions.size() * sizeof(float);
        const auto leafIndexBytes = leafIndices.size() * sizeof(std::uint32_t);
        std::ofstream leavesGltf(leavesGltfPath, std::ios::trunc);
        leavesGltf << "{\"asset\":{\"version\":\"2.0\"},"
                   << "\"buffers\":[{\"uri\":\"leaves.bin\",\"byteLength\":" << leafPositionBytes + leafIndexBytes << "}],"
                   << "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":" << leafPositionBytes << ",\"target\":34962},"
                   << "{\"buffer\":0,\"byteOffset\":" << leafPositionBytes << ",\"byteLength\":" << leafIndexBytes << ",\"target\":34963}],"
                   << "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":" << leafPositions.size() / 3
                   << ",\"type\":\"VEC3\",\"min\":[-3.2,2,-3.2],\"max\":[3.2,6.2,3.2]},"
                   << "{\"bufferView\":1,\"componentType\":5125,\"count\":" << leafIndices.size() << ",\"type\":\"SCALAR\"}],"
                   << "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
                   << "\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
        leavesGltf.close();
        // The coarsest LOD's share of the triangles, or -1 when the chain is malformed.
        const auto coarsest = [&](bool vegetation) -> double
        {
            assetimport::MeshImportOptions leafOptions{true, true, true};
            leafOptions.vegetationLods = vegetation;
            const auto asset = importer.ImportMeshSourceAsset(leavesGltfPath.string(), leafOptions);
            if (asset.submeshes.size() != 1) return -1.0;
            const auto &lods = asset.submeshes.front().lods;
            for (std::size_t lod = 1; lod < lods.size(); ++lod)
                if (lods[lod].indexCount > lods[lod - 1].indexCount || lods[lod].indexCount % 3 != 0) return -1.0;
            return static_cast<double>(lods.back().indexCount) / static_cast<double>(lods.front().indexCount);
        };
        const double standardRatio = coarsest(false), vegetationRatio = coarsest(true);
        std::cout << "Leaf-card LODs: standard coarsest " << standardRatio << ", vegetation " << vegetationRatio << '\n';
        assert(standardRatio > 0.0 && vegetationRatio > 0.0);
        assert(vegetationRatio <= 0.05);
        assert(vegetationRatio < standardRatio * 0.5);
        assetimport::MeshImportOptions flags{true, true, true, true};
        assert(flags.ToFlags() == 15 && assetimport::MeshImportOptions::FromFlags(15).vegetationLods);

        // A triangle budget replaces the source with a simplified base before the LODs are built: the finest LOD
        // stays near the budget, the source's geometry is gone, and every remaining vertex is referenced.
        assetimport::MeshImportOptions budgeted{true, true, true, true, 1000};
        const auto budgetedAsset = importer.ImportMeshSourceAsset(leavesGltfPath.string(), budgeted);
        Expect(budgetedAsset.submeshes.size() == 1, "A budgeted import must keep one submesh");
        const auto &budgetedLods = budgetedAsset.submeshes.front().lods;
        const auto budgetedTriangles = budgetedLods.front().indexCount / 3;
        std::cout << "Leaf-card budget 1000: LOD0 " << budgetedTriangles << " triangles, "
                  << budgetedAsset.meshData.vertices.size() << " vertices, " << budgetedLods.size() << " LODs\n";
        Expect(budgetedTriangles > 0 && budgetedTriangles <= 2000, "A budgeted import must keep its finest LOD near the budget");
        Expect(budgetedAsset.meshData.vertices.size() < leafPositions.size() / 3, "A budgeted import must drop the source's vertices");
        std::vector<bool> referenced(budgetedAsset.meshData.vertices.size());
        for (const auto index : budgetedAsset.meshData.indices) referenced.at(index) = true;
        Expect(std::ranges::all_of(referenced, [](bool used) { return used; }), "A budgeted import must leave no unreferenced vertex");
    }
    {
        // ApplyTriangleBudget leaves a mesh within its budget, and a skinned mesh, unchanged.
        render::MeshData quad{{{{0, 0, 0}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
                               {{1, 1, 0}, {0, 0, 1}, {1, 1}, {1, 0, 0, 1}}, {{0, 1, 0}, {0, 0, 1}, {0, 1}, {1, 0, 0, 1}}},
                              {0, 1, 2, 0, 2, 3}};
        std::vector<render::Submesh> quadSubmeshes(1);
        quadSubmeshes.front().indexCount = 6;
        const auto within = assetimport::ApplyTriangleBudget(quad, quadSubmeshes, 2,
                                                            assetimport::TriangleBudgetSimplification::PreserveTopology);
        Expect(!within.applied && within.sourceTriangles == 2 && quad.indices.size() == 6, "A mesh within its budget must stay unchanged");
        quad.vertices.front().weights = {1, 0, 0, 0};
        const auto skinned = assetimport::ApplyTriangleBudget(quad, quadSubmeshes, 1,
                                                             assetimport::TriangleBudgetSimplification::PruneIslands);
        Expect(!skinned.applied && quad.indices.size() == 6, "A skinned mesh must ignore the budget");

        // Cards keep their coverage: 2000 separate unit quads (4000 triangles) at a 2000-triangle budget keep about
        // half of the cards, each enlarged so that the total area stays about the source's.
        render::MeshData cards;
        for (std::uint32_t card = 0; card < 2000; ++card)
        {
            const float x = static_cast<float>(card % 50) * 3.0f, z = static_cast<float>(card / 50) * 3.0f;
            const auto base = static_cast<std::uint32_t>(cards.vertices.size());
            for (const auto &[dx, dy] : {std::pair(0.0f, 0.0f), std::pair(1.0f, 0.0f), std::pair(1.0f, 1.0f), std::pair(0.0f, 1.0f)})
                cards.vertices.push_back({{x + dx, dy, z}, {0, 0, 1}, {dx, dy}, {1, 0, 0, 1}});
            cards.indices.insert(cards.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        std::vector<render::Submesh> cardSubmeshes(1);
        cardSubmeshes.front().indexCount = static_cast<std::uint32_t>(cards.indices.size());
        const std::array surfaces{assetimport::TriangleBudgetSurface::Cards};
        const auto reduced = assetimport::ApplyTriangleBudget(cards, cardSubmeshes, 2000,
                                                             assetimport::TriangleBudgetSimplification::PruneIslands, surfaces);
        float area = 0.0f;
        for (std::size_t i = 0; i < cards.indices.size(); i += 3)
        {
            const auto position = [&](std::size_t corner)
            {
                const auto &p = cards.vertices[cards.indices[i + corner]].position;
                return glm::vec3(p[0], p[1], p[2]);
            };
            area += 0.5f * glm::length(glm::cross(position(1) - position(0), position(2) - position(0)));
        }
        std::cout << "Card budget 2000: " << reduced.keptTriangles << " triangles, area " << area << " of 2000\n";
        Expect(reduced.applied && reduced.keptTriangles <= 2000 && reduced.keptTriangles >= 1000,
               "Cards must be reduced to their budget by whole cards");
        Expect(area > 1700.0f && area < 2300.0f, "Dropped cards must be made up by enlarging the rest");
    }
    {
        // A modelled leaf (a gently curved 4 x 3 grid, 24 triangles, UVs over one atlas cell) becomes one quad over its
        // UV rectangle; a part whose UVs do not map onto it (a twig, all one texel) stays as it is.
        render::MeshData mesh;
        for (int y = 0; y <= 3; ++y)
            for (int x = 0; x <= 4; ++x)
            {
                const float u = 0.2f + 0.05f * static_cast<float>(x), v = 0.5f + 0.1f * static_cast<float>(y);
                const float bend = 0.02f * static_cast<float>((x - 2) * (x - 2));
                mesh.vertices.push_back({{0.1f * static_cast<float>(x), 0.1f * static_cast<float>(y), bend}, {0, 0, 1}, {u, v},
                                         {1, 0, 0, 1}});
            }
        for (std::uint32_t y = 0; y < 3; ++y)
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                const std::uint32_t a = y * 5 + x;
                mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + 6, a, a + 6, a + 5});
            }
        const auto twig = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int corner = 0; corner < 6; ++corner)
            mesh.vertices.push_back({{2.0f + 0.1f * static_cast<float>(corner % 3), 0.1f * static_cast<float>(corner / 3), 0.0f},
                                     {0, 0, 1}, {0.9f, 0.9f}, {1, 0, 0, 1}});
        mesh.indices.insert(mesh.indices.end(), {twig, twig + 1, twig + 3, twig + 1, twig + 4, twig + 3,
                                                 twig + 1, twig + 2, twig + 4, twig + 2, twig + 5, twig + 4});
        const auto cards = assetimport::MakeLeafCards(mesh, mesh.indices);
        std::cout << "Leaf cards: " << mesh.indices.size() / 3 << " triangles -> " << cards.Triangles() << '\n';
        Expect(cards.Triangles() == 2 + 4, "A modelled leaf must become one quad and a twig must stay as it is");
        bool spansCell = false;
        for (const auto &vertex : cards.vertices)
            spansCell = spansCell || (std::abs(vertex.uv[0] - 0.4f) < 1e-4f && std::abs(vertex.uv[1] - 0.8f) < 1e-4f);
        Expect(spansCell, "A leaf card must span its leaf's UV rectangle");
    }
    // Alternating cook settings must preserve both disk cache variants.
    // Native skinned LODs preserve vertex payloads and the original index range.
    auto skinnedData = generatedAsset.meshData;
    auto skinnedSubmeshes = generatedAsset.submeshes;
    skinnedData.indices.resize(skinnedSubmeshes.front().indexCount);
    for (auto &vertex : skinnedData.vertices)
    {
        vertex.joints = {0, 1, 0, 0};
        vertex.weights = {0.5f, 0.5f, 0, 0};
    }
    const auto originalVertices = skinnedData.vertices;
    const auto originalIndices = skinnedData.indices;
    assetimport::MeshImporter::BuildMeshLods(skinnedData, skinnedSubmeshes);
    assert(skinnedSubmeshes.front().lods.size() > 1);
    assert(std::equal(originalIndices.begin(), originalIndices.end(), skinnedData.indices.begin()));
    assert(skinnedData.vertices.size() == originalVertices.size());
    for (size_t i = 0; i < originalVertices.size(); ++i)
    {
        assert(skinnedData.vertices[i].uv == originalVertices[i].uv);
        assert(skinnedData.vertices[i].joints == originalVertices[i].joints);
        assert(skinnedData.vertices[i].weights == originalVertices[i].weights);
    }
    auto invalid = skinnedSubmeshes;
    invalid.front().indexCount = std::numeric_limits<uint32_t>::max();
    bool rejected = false;
    try { assetimport::MeshImporter::BuildMeshLods(skinnedData, invalid); }
    catch (const std::invalid_argument &) { rejected = true; }
    assert(rejected);
    const auto plainSource = importer.ImportMeshSourceAsset(gridGltfPath.string());
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> cacheStamps;
    for (const auto &entry : std::filesystem::directory_iterator(lodTestDirectory / ".plutoge-cache" / "meshes"))
    {
        if (entry.path().extension() == ".pmesh")
            cacheStamps.emplace_back(entry.path(), entry.last_write_time());
    }
    assert(cacheStamps.size() >= 2);
    const auto cachedSource = importer.ImportMeshSourceAsset(gridGltfPath.string(), options);
    assert(cachedSource.meshData.indices == generatedAsset.meshData.indices);
    for (const auto &[path, stamp] : cacheStamps)
        assert(std::filesystem::last_write_time(path) == stamp);

    // Repeated LOD requests and alternating settings reuse the same live meshes.
    const auto lodMesh = importer.GenerateMeshLods(gridGltfPath.string(), options);
    const auto plainMesh = importer.ImportMeshAsset(gridGltfPath.string());
    assert(lodMesh.mesh != nullptr);
    assert(plainMesh.mesh != nullptr);
    assert(lodMesh.mesh != plainMesh.mesh);
    assert(importer.GenerateMeshLods(gridGltfPath.string(), options).mesh == lodMesh.mesh);
    assert(importer.ImportMeshAsset(gridGltfPath.string()).mesh == plainMesh.mesh);

    // Source edits still invalidate the corresponding memory and disk entries.
    std::ofstream changedSource(gridGltfPath, std::ios::app);
    changedSource << ' ';
    changedSource.close();
    assert(importer.GenerateMeshLods(gridGltfPath.string(), options).mesh != lodMesh.mesh);
    std::filesystem::remove_all(lodTestDirectory);
    return 0;
}
