#pragma once
#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/assets/ModelInstanceState.h"
#include <memory>

namespace PlutoGE::assets
{
    struct ModelGenerationSnapshot
    {
        content::ContentDigest generation{};
        bool authoredSnapshot = false;
        ModelGeneratedFile packageArtifact;
        ModelAsset package;
        ModelHierarchyAsset hierarchy;
        std::shared_ptr<const AssetCatalog> catalog;
        std::shared_ptr<const AssetStorageMap> storage;
    };

    struct StaticModelGenerationSnapshot
    {
        ModelGenerationSnapshot artifacts;
        StaticModelInstanceGeneration generation;
        std::vector<std::string> defaultMaterials;
    };

    // Decode the exact native mesh inventory in a private CPU reader, verify
    // provenance and rigid bindings, and require resolved source-node identities.
    // No GPU allocation or scene publication; failure preserves caller output.
    bool PrepareStaticModelGenerationSnapshot(const Project &project,
        const ModelGenerationSnapshot &artifacts, StaticModelGenerationSnapshot &output,
        std::string *errorMessage = nullptr);

    // Validate exact persisted baseline evidence against a prepared generation.
    // This includes decoded layout/counts/defaults, package proof and generation.
    bool ValidateStaticModelInstanceBaseline(const StaticModelInstanceState &state,
        const StaticModelGenerationSnapshot &generation, std::string *errorMessage = nullptr);

    // Read an explicitly accepted package, independent of the active source's
    // current metadata/correspondence. Verifies package, hierarchy and all owned
    // object bytes. Library reads retain a shared generation lease; immutable
    // authored snapshots remain readable without Library. The descriptor
    // must come from the accepted import, never from filename guessing.
    // Generation-scoped virtual locations prevent an old package from shadowing
    // unrelated authored/current assets. Use this snapshot in a dedicated reader;
    // never publish it as the project's current catalog. Source-owned logical
    // identities retain their meaning inside that reader. No writes or GPU work.
    // Complete private artifact baselines are required; failure preserves output.
    bool ReadModelGenerationSnapshot(const Project &project, const std::string &sourceAssetId,
        const content::ContentDigest &generation, const ModelGeneratedFile &packageArtifact,
        const std::shared_ptr<const AssetCatalog> &currentCatalog,
        const std::shared_ptr<const AssetStorageMap> &currentStorage,
        ModelGenerationSnapshot &output, std::string *errorMessage = nullptr);
}
