#include "ModelGenerationPublication.h"
#include <algorithm>

namespace PlutoGE::assetimport
{
    bool UsesLibraryModelStorage(const assets::Project &project)
    { return project.GetManifest().assetPipelineVersion >= 3; }

    ArtifactManifest ModelPublishedGeneration(const assets::Project &project,
        const ArtifactManifest &generation, const std::filesystem::path &sourceMetadata)
    {
        auto publication = generation;
        if (UsesLibraryModelStorage(project))
        {
            const auto metadata = sourceMetadata.lexically_relative(project.GetAssetDirectoryPath());
            std::erase_if(publication.outputs, [&](const auto &output)
            { return output.relativePath != metadata && output.relativePath.extension() != ".materials"; });
        }
        return publication;
    }

    bool ValidateModelGenerationPublication(const assets::Project &project,
        const ArtifactManifest &generation, const std::filesystem::path &sourceMetadata,
        std::string *errorMessage)
    {
        if (UsesLibraryModelStorage(project))
        {
            ArtifactCache cache(project.GetRootDirectory() / "Library" / "Artifacts");
            ArtifactManifest verified;
            if (cache.Find(generation.key, verified, errorMessage) != ArtifactCacheStatus::Hit) return false;
        }
        return ValidateArtifactPublication(ModelPublishedGeneration(project, generation, sourceMetadata),
            project.GetAssetDirectoryPath(), errorMessage);
    }
}
