#pragma once
#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/assets/Project.h"

namespace PlutoGE::assets { struct ModelGeneratedFile; }

namespace PlutoGE::assetimport
{
    bool FindModelPackageArtifact(const ArtifactManifest &generation, assets::ModelGeneratedFile &output, std::string *errorMessage);
    // Version 1/2 keep their native layout; version 3 publishes authored files
    // only, with source metadata selecting an immutable Library generation.
    bool UsesLibraryModelStorage(const assets::Project &project);
    ArtifactManifest ModelPublishedGeneration(const assets::Project &project,
        const ArtifactManifest &generation, const std::filesystem::path &sourceMetadata);
    bool ValidateModelGenerationPublication(const assets::Project &project,
        const ArtifactManifest &generation, const std::filesystem::path &sourceMetadata,
        std::string *errorMessage);
}
