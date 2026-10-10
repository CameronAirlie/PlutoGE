#include "ModelCacheRestore.h"
#include "ModelGenerationPublication.h"
#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ImportFileTransaction.h"
#include "PlutoGE/asset_import/ModelArtifactSettings.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelImportSettings.h"
#include <algorithm>
#include <set>

namespace PlutoGE::assetimport
{
    ModelCacheRestoreStatus RestoreCachedModel(const ModelCacheRestoreContext &context,
                                               ModelImportResult &result, std::string *errorMessage)
    {
        const auto &project = context.project;
        const auto manifestPath = assets::GetModelManifestPath(project, context.request.sourceReference);
        if (manifestPath != context.previousManifest) return ModelCacheRestoreStatus::Miss;
        const auto assetRoot = project.GetAssetDirectoryPath();
        const auto metadataPath = assets::GetAssetMetadataPath(project.ResolveAssetReference(context.request.sourceReference));
        const bool persistent = project.GetManifest().assetPipelineVersion >= 2;
        auto reference = [&](const std::filesystem::path &relative)
        {
            const auto text = relative.generic_u8string();
            return std::string(assets::Project::kProjectAssetScheme) + std::string(reinterpret_cast<const char *>(text.data()), text.size());
        };
        const auto manifestReference = reference(manifestPath.lexically_relative(assetRoot));
        const auto metadataReference = reference(metadataPath.lexically_relative(assetRoot));
        auto permitted = context.generatedLocations;
        assets::ModelHierarchyArtifact hierarchy;
        const auto hierarchyStatus = assets::ReadModelHierarchyArtifact(context.previous, hierarchy, errorMessage);
        if (hierarchyStatus != assets::ModelHierarchyArtifactStatus::Success && hierarchyStatus != assets::ModelHierarchyArtifactStatus::Missing)
            return ModelCacheRestoreStatus::Failed;
        if (hierarchyStatus == assets::ModelHierarchyArtifactStatus::Success) permitted.insert(hierarchy.reference);
        if (persistent) permitted.insert(metadataReference);
        for (const auto &input : context.authoredInputs)
            if (input.identity.starts_with("material-overrides/")) permitted.insert(reference(input.path.lexically_relative(assetRoot)));

        // Compare resolved aliases so legacy physical and logical bindings
        // agree. A moved authored material retains its main identity.
        assets::ModelImportSettings settings;
        if (assets::ReadModelImportSettings(context.metadata, settings) == assets::ModelImportSettingsStatus::Success)
        {
            assets::AssetManager resolver;
            resolver.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory);
            resolver.SetAssetCatalog(context.catalog);
            for (const auto &remap : settings.materialRemaps)
            {
                const auto target = remap.engineMaterial.empty() ? resolver.ResolveStableAssetId(remap.authoredMaterial.assetId) : remap.engineMaterial;
                if (target.empty() || std::none_of(context.bindings.begin(), context.bindings.end(), [&](const auto &binding)
                    { return resolver.ResolveAssetPath(binding) == resolver.ResolveAssetPath(target); }))
                    return ModelCacheRestoreStatus::Miss;
            }
        }
        ArtifactCache cache(project.GetRootDirectory() / "Library" / "Artifacts");
        auto matches = [&](const ArtifactManifest &candidate)
        {
            if (candidate.recipe.importer != kModelArtifactImporter || candidate.recipe.version != kModelArtifactVersion ||
                candidate.recipe.target != kModelArtifactTarget) return false;
            std::vector<std::filesystem::path> outputs;
            std::set<std::string> locations;
            for (const auto &output : candidate.outputs)
            {
                const auto location = reference(output.relativePath);
                if (!permitted.contains(location)) return false;
                locations.insert(location);
                outputs.push_back(output.relativePath);
            }
            if (hierarchyStatus == assets::ModelHierarchyArtifactStatus::Success && !locations.contains(hierarchy.reference)) return false;
            if (!locations.contains(manifestReference) || locations.contains(metadataReference) != persistent) return false;
            for (const auto &object : context.previous.objects)
            {
                if (object.type != assets::ProjectAssetType::Texture && !locations.contains(object.reference)) return false;
                if (object.type == assets::ProjectAssetType::Mesh)
                {
                    const auto overridePath = std::filesystem::path(project.ResolveAssetReference(object.reference)).concat(".materials");
                    if (std::filesystem::is_regular_file(overridePath) != locations.contains(object.reference + ".materials")) return false;
                }
            }
            content::ContentDigest expected;
            if (!ComputeModelArtifactSettings(context.metadata, context.options, context.request.sourceReference,
                                               context.bindings, outputs, expected, nullptr, UsesLibraryModelStorage(project)) || expected != candidate.recipe.settings) return false;
            if (candidate.recipe.inputs.empty() || !AreArtifactInputsCurrent(candidate.recipe)) return false;
            // Protect raw authored files separately from generation semantics.
            ArtifactRecipe authored;
            authored.inputs = context.authoredInputs;
            return AreArtifactInputsCurrent(authored);
        };
        ArtifactManifest generation;
        // Cache availability is an optimization: unavailable or invalid entries
        // take the normal importer path, preserving useful import diagnostics.
        if (cache.FindMatchingForRequest("model/" + context.metadata.id, matches, generation) != ArtifactCacheStatus::Hit) return ModelCacheRestoreStatus::Miss;
        context.progress("Reusing cached artifacts");
        ImportFileTransaction transaction(project.GetRootDirectory());
        std::vector<std::filesystem::path> outputs;
        const auto publication = ModelPublishedGeneration(project, generation, metadataPath);
        for (const auto &output : publication.outputs)
        {
            const auto destination = transaction.GetOutputRoot() / output.relativePath;
            std::filesystem::create_directories(destination.parent_path());
            std::filesystem::copy_file(cache.GetDirectory(generation.key) / "Files" / output.relativePath, destination);
            content::ContentDigest copied;
            if (!content::HashFileContent(destination, copied, errorMessage) || copied != output.digest)
            {
                if (errorMessage) *errorMessage = "Cached artifact changed while restoring.";
                return ModelCacheRestoreStatus::Failed;
            }
            outputs.push_back(output.relativePath);
        }
        context.progress("Caching artifacts");
        context.progress("Publishing artifacts");
        if (!matches(generation))
        {
            if (errorMessage) *errorMessage = "Import inputs changed while restoring cached artifacts.";
            return ModelCacheRestoreStatus::Failed;
        }
        if (!transaction.Publish(assetRoot, outputs, errorMessage)) return ModelCacheRestoreStatus::Failed;
        context.progress("Validating asset database");
        assets::AssetDatabase database;
        if (!database.Scan(context.project, assets::AssetScanOptions{.hashContent=false, .collectDependencies=false}, errorMessage)) return ModelCacheRestoreStatus::Failed;
        std::vector<std::string> changedAssets;
        for (const auto &output : generation.outputs) changedAssets.push_back(reference(output.relativePath));
        ModelImportResult candidate{database.GetCatalog(), UsesLibraryModelStorage(project) ? context.request.sourceReference : manifestReference, true, std::move(changedAssets), context.metadata.id, database.GetStorageMap()};
        candidate.artifactGenerationKey = generation.key;
        if (!FindModelPackageArtifact(generation, candidate.packageArtifact, errorMessage)) return ModelCacheRestoreStatus::Failed;
        context.progress("Import complete");
        ImportState acceptedState;
        if (!CaptureImportState(context.metadata.id, context.request.sourceReference, generation, context.authoredInputs, acceptedState, errorMessage)) return ModelCacheRestoreStatus::Failed;
        if (!ValidateModelGenerationPublication(project, generation, metadataPath, errorMessage)) return ModelCacheRestoreStatus::Failed;
        if (!transaction.Accept(errorMessage)) return ModelCacheRestoreStatus::Failed;
        RememberAcceptedImport(context.project, acceptedState);
        result = std::move(candidate);
        if (errorMessage) errorMessage->clear();
        return ModelCacheRestoreStatus::Restored;
    }
}
