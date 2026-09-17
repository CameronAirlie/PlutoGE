#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/import/MeshImporter.h"
#include <iostream>

// Prepare a replacement artifact without overwriting the project's mesh.
// Arguments: project directory, source FBX, existing mesh reference, output mesh.
int main(int argc, char **argv)
{
    if (argc != 5)
        return 2;
    try
    {
        PlutoGE::assets::AssetManager manager;
        manager.SetProjectContext(argv[1]);
        const auto references = manager.GetMeshAssetMaterialReferences(argv[3]);
        const auto metadata = manager.GetMeshAssetMetadata(argv[3]);
        auto *oldMesh = manager.LoadMeshAsset(argv[3]);
        if (!oldMesh)
            throw std::runtime_error("Could not load existing mesh");
        auto imported = PlutoGE::assetimport::MeshImporter{}.ImportMeshSourceAsset(argv[2], metadata.importOptions);
        if (oldMesh->GetSubmeshCount() != imported.submeshes.size())
            throw std::runtime_error("Submesh count changed; requires full editor reimport");
        for (size_t i = 0; i < imported.submeshes.size(); ++i)
            if (oldMesh->GetSubmesh(i).name != imported.submeshes[i].name ||
                oldMesh->GetSubmesh(i).materialIndex != imported.submeshes[i].materialIndex)
                throw std::runtime_error("Submesh identity changed; requires full editor reimport");
        PlutoGE::render::MeshConfig config;
        config.data = std::move(imported.meshData);
        config.submeshes = std::move(imported.submeshes);
        config.hasLightmapUvs = imported.hasLightmapUvs;
        config.skeleton = std::move(imported.skeleton);
        config.animationNodes = std::move(imported.animationNodes);
        config.animations = std::move(imported.animations);
        std::string error;
        if (!manager.SaveMeshAsset(argv[4], config, references, &error, metadata))
            throw std::runtime_error(error);
        std::cout << "Prepared " << argv[4] << " preserving " << config.submeshes.size() << " submesh identities and "
                  << references.size() << " material bindings\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
