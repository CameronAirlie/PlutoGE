#pragma once

#include "PlutoGE/assets/AssetCatalog.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::assets { class AssetManager; }
namespace PlutoGE::render { class Material; }
namespace PlutoGE::scene
{
    struct ModelAssetSnapshot
    {
        std::string sourceAssetId;
        std::shared_ptr<const assets::AssetCatalog> catalog;
        std::unordered_map<std::uint64_t, std::vector<std::string>> meshMaterialBindings;
        std::unordered_map<std::uint64_t, std::vector<render::Material *>> meshMaterials;
    };
    struct AssetReconciliationReport
    {
        std::size_t refreshedMeshes = 0;
        std::size_t unresolvedMeshes = 0;
        std::vector<std::string> diagnostics;
    };
    // Resource-owning thread; captures CPU bindings before worker publication.
    ModelAssetSnapshot CaptureModelAssetSnapshot(assets::AssetManager &manager, const std::string &sourceAssetId);
}
