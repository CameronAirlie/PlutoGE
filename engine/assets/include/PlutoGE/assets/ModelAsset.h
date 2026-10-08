#pragma once

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/platform/ContentDigest.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace PlutoGE::assets
{
    struct ModelSubAsset
    {
        std::uint64_t localId = 0;
        ProjectAssetType type = ProjectAssetType::Unknown;
        std::string name;
        std::string reference;
    };

    struct ModelGeneratedFile
    {
        std::string reference;
        content::ContentDigest digest{};
    };

    struct ModelAsset
    {
        std::string sourceReference;
        std::string sourceAssetId;
        std::uint64_t sourceContentHash = 0;
        std::uint32_t importerVersion = 1;
        std::vector<ModelSubAsset> objects;
        // Optional provenance baselines, retained with the source package.
        std::vector<ModelGeneratedFile> generatedFiles;
        std::vector<std::string> extensionRecords;
    };

    std::uint64_t MakeModelSubAssetId(ProjectAssetType type, std::string_view name);
    // These helpers define virtual project locations. Versions 1/2 publish
    // native files here; version 3 uses the same relative layout inside the
    // immutable Library generation selected by persistent source metadata.
    // Version 3 optionally uses the strict MODEL_OUTPUT_DIRECTORY v1 source
    // setting to isolate virtual products from preserved legacy authored files.
    std::filesystem::path GetModelArtifactDirectory(const Project &project, std::string_view sourceReference);
    std::filesystem::path GetModelManifestPath(const Project &project, std::string_view sourceReference);
    // Reads old Imported/<name> manifests only as a compatibility fallback.
    std::filesystem::path FindModelManifestPath(const Project &project, std::string_view sourceReference);
    // Resolve the editable mesh and importer-owned material bindings consistently
    // for model drops in both the viewport and hierarchy. Does not import/write assets.
    bool ResolveModelPlacementMesh(const Project &project, std::string_view modelReference,
                                   std::string &meshReference, std::string &materialBindingReference,
                                   std::string *errorMessage = nullptr);
    bool SerializeModelAsset(const ModelAsset &asset, std::string &text, std::string *errorMessage = nullptr);
    bool ParseModelAsset(std::string_view text, ModelAsset &asset, std::string *errorMessage = nullptr);
    bool SaveModelAsset(const std::string &path, const ModelAsset &asset, std::string *errorMessage = nullptr);
    bool LoadModelAsset(const std::string &path, ModelAsset &asset, std::string *errorMessage = nullptr);
}
