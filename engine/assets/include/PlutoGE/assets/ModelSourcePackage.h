#pragma once
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/ModelAsset.h"

namespace PlutoGE::assets
{
    enum class ModelSourcePackageStatus { Success, Missing, UnsupportedVersion, Invalid };
    // Persistent source-owned object locations and provenance. Library and native
    // manifest copies can be rebuilt from this snapshot; it is not a cache index.
    // Failures preserve caller outputs and unrelated metadata records.
    ModelSourcePackageStatus ReadModelSourcePackage(const AssetMetadata &metadata, ModelAsset &package,
                                                    std::string *errorMessage = nullptr);
    // Source metadata is authoritative when present; legacy packages use their
    // native manifest. Performs no writes and retains output on failure.
    bool LoadModelSourcePackage(const Project &project, std::string_view sourceReference, ModelAsset &package,
                                std::string *errorMessage = nullptr);
    bool WriteModelSourcePackage(AssetMetadata &metadata, const ModelAsset &package,
                                 std::string *errorMessage = nullptr);
}
