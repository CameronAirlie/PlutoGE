#pragma once

#include "PlutoGE/assets/Project.h"

#include <cstdint>
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
    bool SaveModelAsset(const std::string &path, const ModelAsset &asset, std::string *errorMessage = nullptr);
    bool LoadModelAsset(const std::string &path, ModelAsset &asset, std::string *errorMessage = nullptr);
}
