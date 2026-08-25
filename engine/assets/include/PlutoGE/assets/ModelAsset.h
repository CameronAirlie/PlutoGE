#pragma once

#include "PlutoGE/assets/Project.h"

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

    struct ModelAsset
    {
        std::string sourceReference;
        std::string sourceAssetId;
        std::uint64_t sourceContentHash = 0;
        std::uint32_t importerVersion = 1;
        std::vector<ModelSubAsset> objects;
    };

    std::uint64_t MakeModelSubAssetId(ProjectAssetType type, std::string_view name);
    // Imported artifacts are part of the source model's package. Keeping the
    // source, manifest, meshes, materials, textures, and clips together avoids
    // two competing asset trees and gives every model instance one canonical
    // set of editable dependencies.
    std::filesystem::path GetModelArtifactDirectory(const Project &project, std::string_view sourceReference);
    std::filesystem::path GetModelManifestPath(const Project &project, std::string_view sourceReference);
    // Reads old Imported/<name> manifests only as a compatibility fallback.
    std::filesystem::path FindModelManifestPath(const Project &project, std::string_view sourceReference);
    bool SaveModelAsset(const std::string &path, const ModelAsset &asset, std::string *errorMessage = nullptr);
    bool LoadModelAsset(const std::string &path, ModelAsset &asset, std::string *errorMessage = nullptr);
}
