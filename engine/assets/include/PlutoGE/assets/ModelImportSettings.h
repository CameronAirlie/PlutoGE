#pragma once

#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetReference.h"
#include "PlutoGE/import/MeshImportOptions.h"

#include <string>
#include <vector>

namespace PlutoGE::assets
{
    struct ModelObjectIdentity
    {
        std::string sourceKey;
        std::uint64_t localId = 0;
        bool retired = false;
    };

    struct ModelMaterialRemap
    {
        std::uint64_t materialLocalId = 0;
        AssetReference authoredMaterial;
        std::string engineMaterial; // Alternative to a project-owned material identity.
    };

    struct ModelNodeAlias
    {
        std::string sourceKey;
        std::string canonicalSourceKey;
    };

    struct ModelImportSettings
    {
        assetimport::MeshImportOptions meshOptions{true, true, true};
        std::vector<ModelObjectIdentity> objects;
        std::vector<ModelMaterialRemap> materialRemaps;
        std::vector<ModelNodeAlias> nodeAliases;
    };

    enum class ModelImportSettingsStatus { Success, Missing, UnsupportedVersion, Invalid };

    ModelImportSettingsStatus ReadModelImportSettings(const AssetMetadata &metadata, ModelImportSettings &settings,
                                                     std::string *errorMessage = nullptr);
    // Caller must require project format >= 2 before persisting these records.
    // Node aliases additionally require project format >= 5 and settings codec 2.
    // Unknown records remain intact; failures leave metadata unchanged.
    bool WriteModelImportSettings(AssetMetadata &metadata, const ModelImportSettings &settings,
                                  std::string *errorMessage = nullptr);

    // Mark all prior entries retired before processing a new source snapshot.
    // Matching keys reactivate their previous ID; new keys never recycle IDs.
    void BeginModelObjectImport(ModelImportSettings &settings);
    std::uint64_t ResolveModelObjectId(ModelImportSettings &settings, std::string_view sourceKey,
                                       std::uint64_t legacyId = 0, std::string *errorMessage = nullptr);
}
