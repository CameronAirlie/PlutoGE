#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/import/MeshImporter.h"
#include "SkinningLodLayout.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
    // Triangles per LOD level, the submeshes that have generated LODs, and the
    // vertices GPU skinning deforms while no draw is finer than each level.
    void Report(const PlutoGE::render::Mesh &mesh)
    {
        std::size_t levels = 1, withLods = 0;
        for (size_t i = 0; i < mesh.GetSubmeshCount(); ++i)
        {
            levels = std::max(levels, mesh.GetSubmeshLodCount(i));
            withLods += mesh.GetSubmeshLodCount(i) > 1;
        }
        const auto layout = PlutoGE::render::BuildSkinningLodLayout(mesh);
        for (size_t lod = 0; lod < levels; ++lod)
        {
            size_t triangles = 0;
            for (size_t i = 0; i < mesh.GetSubmeshCount(); ++i)
                triangles += mesh.GetSubmeshLodRange(i, std::min(lod, mesh.GetSubmeshLodCount(i) - 1)).indexCount / 3;
            std::cout << "LOD" << lod << ": " << triangles << " triangles, "
                      << layout.VertexCount(static_cast<std::uint32_t>(lod)) << " skinned vertices\n";
        }
        std::cout << withLods << " of " << mesh.GetSubmeshCount() << " submeshes have generated LODs; "
                  << mesh.GetVertexCount() << " vertices, " << mesh.GetSkeleton().joints.size() << " joints\n";
    }
}

// Offline, non-destructive preparation: project-root input-reference output-path [--flip-v].
// UV conversion is explicit because already-correct native meshes must not be flipped.
// project-root input-reference --report prints an existing asset's LODs without writing.
int main(int argc, char **argv)
{
    try
    {
        if (argc == 4 && std::string_view(argv[3]) == "--report")
        {
            PlutoGE::assets::AssetManager manager;
            manager.SetProjectContext(argv[1]);
            const auto *mesh = manager.LoadMeshAsset(argv[2]);
            if (!mesh) throw std::runtime_error("Could not load native mesh");
            Report(*mesh);
            return 0;
        }
        if (argc < 4 || argc > 5 || (argc == 5 && std::string_view(argv[4]) != "--flip-v"))
            throw std::invalid_argument(
                "Usage: PrepareNativeMeshLods project-root input-reference (output-path [--flip-v] | --report)");
        PlutoGE::assets::AssetManager manager;
        manager.SetProjectContext(argv[1]);
        if (std::filesystem::exists(argv[3]))
            throw std::invalid_argument("Output already exists; select a new artifact path");
        auto *mesh = manager.LoadMeshAsset(argv[2]);
        if (!mesh) throw std::runtime_error("Could not load native source mesh");
        PlutoGE::render::MeshConfig config;
        config.data = mesh->GetMeshData();
        config.skeleton = mesh->GetSkeleton();
        config.animationNodes = mesh->GetAnimationNodes();
        config.animations = mesh->GetAnimations();
        config.hasLightmapUvs = mesh->HasLightmapUvs();
        for (size_t i = 0; i < mesh->GetSubmeshCount(); ++i)
        {
            if (mesh->GetSubmeshLodCount(i) > 1)
                throw std::invalid_argument("Input already has LODs; regenerate from its preserved LOD0 source");
            config.submeshes.push_back(mesh->GetSubmesh(i));
        }
        if (argc == 5)
        {
            for (auto &vertex : config.data.vertices)
            {
                vertex.uv[1] = 1.0f - vertex.uv[1];
                vertex.tangent = {0, 0, 0, 1};
            }
            // Rebuild tangent space from corrected UVs, not arbitrary exported axes.
            PlutoGE::render::Mesh corrected(config);
            config.data = corrected.GetMeshData();
        }
        PlutoGE::assetimport::MeshImporter::BuildMeshLods(config.data, config.submeshes);
        auto metadata = manager.GetMeshAssetMetadata(argv[2]);
        metadata.importOptions.generateLods = true;
        metadata.importOptions.optimizeVertexCache = true;
        std::string error;
        if (!manager.SaveMeshAsset(argv[3], config, manager.GetMeshAssetMaterialReferences(argv[2]), &error, metadata))
            throw std::runtime_error(error);
        Report(PlutoGE::render::Mesh(config));
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
