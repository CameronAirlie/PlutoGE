#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"

#include <string>
#include <vector>

namespace PlutoGE::assets { class AssetManager; }

namespace PlutoGE::scene
{
    class Entity;

    struct PrefabMeshExportData
    {
        render::MeshConfig mesh;
        std::vector<std::string> materialReferences;
        std::vector<render::MaterialConfig> embeddedMaterialConfigs;
        std::vector<std::uint8_t> hasEmbeddedMaterial;
        std::size_t meshComponentCount = 0;
        std::size_t submeshCount = 0;
    };

    bool BuildStaticMeshFromEntityHierarchy(const Entity &root,
                                            assets::AssetManager &assetManager,
                                            PrefabMeshExportData &output,
                                            std::string *errorMessage = nullptr);

    bool ExportPrefabToStaticMeshAsset(const std::string &prefabReference,
                                       const std::string &meshAssetReference,
                                       assets::AssetManager &assetManager,
                                       std::string *errorMessage = nullptr);
}
