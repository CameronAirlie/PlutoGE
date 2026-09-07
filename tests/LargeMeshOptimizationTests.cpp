#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/render/IndirectDraw.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
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

int main(int argc, char **argv)
{
    using namespace PlutoGE;

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

    const auto command = render::BuildDrawElementsIndirectCommand(300, 4, 27, 12);
    assert(command.count == 300);
    assert(command.instanceCount == 4);
    assert(command.firstIndex == 27);
    assert(command.baseVertex == 0);
    assert(command.baseInstance == 12);

    int shader = 0;
    int materialA = 0;
    int materialB = 0;
    int meshA = 0;
    int meshB = 0;
    const render::IndirectDrawGroupingKey base{&shader, &materialA, &meshA, false, false};
    assert(render::CanGroupGeometryIndirectDraws(base, base));
    assert(!render::CanGroupGeometryIndirectDraws(base, {&shader, &materialB, &meshA, false, false}));
    assert(!render::CanGroupGeometryIndirectDraws(base, {&shader, &materialA, &meshB, false, false}));
    assert(!render::CanGroupGeometryIndirectDraws(base, {&shader, &materialA, &meshA, true, false}));

    assert(render::CanGroupShadowIndirectDraws(base, {nullptr, &materialB, &meshA, false, false}));
    const render::IndirectDrawGroupingKey masked{nullptr, &materialA, &meshA, false, true};
    assert(render::CanGroupShadowIndirectDraws(masked, masked));
    assert(!render::CanGroupShadowIndirectDraws(masked, {nullptr, &materialB, &meshA, false, true}));
    assert(!render::CanGroupShadowIndirectDraws(masked, {nullptr, &materialA, &meshA, true, true}));

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
    // Alternating cook settings must preserve both disk cache variants.
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
