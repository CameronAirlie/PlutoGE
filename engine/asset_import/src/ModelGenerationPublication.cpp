#include "ModelGenerationPublication.h"
#include <algorithm>
#include "PlutoGE/assets/ModelAsset.h"

namespace PlutoGE::assetimport
{
    bool FindModelPackageArtifact(const ArtifactManifest &generation, assets::ModelGeneratedFile &output, std::string *errorMessage)
    {
        assets::ModelGeneratedFile candidate;
        for (const auto &file : generation.outputs)
            if (file.relativePath.extension() == ".plutomodel")
            {
                if (!candidate.reference.empty())
                { if (errorMessage) *errorMessage = "Model generation has ambiguous package artifacts."; return false; }
                candidate = {"project://" + file.relativePath.generic_string(), file.digest};
            }
        if (candidate.reference.empty())
        { if (errorMessage) *errorMessage = "Model generation has no package artifact."; return false; }
        output = std::move(candidate);
        return true;
    }

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
