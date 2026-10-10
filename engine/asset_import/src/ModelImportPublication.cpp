#include "PlutoGE/asset_import/ModelImportPublication.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ModelActiveGeneration.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
namespace PlutoGE::assetimport
{
    PreparedModelImportPublication::PreparedModelImportPublication() = default;
    PreparedModelImportPublication::~PreparedModelImportPublication() = default;
    PreparedModelImportPublication::PreparedModelImportPublication(PreparedModelImportPublication &&) noexcept = default;
    PreparedModelImportPublication &PreparedModelImportPublication::operator=(PreparedModelImportPublication &&) noexcept = default;

    bool PrepareModelImportPublication(const assets::Project &project,
        const std::string &sourceReference, const ModelImportResult &result,
        PreparedModelImportPublication &output, std::string *error)
    {
        if (project.GetManifest().assetPipelineVersion < 3 || !assets::Project::IsProjectAssetReference(sourceReference) ||
            assets::Project::GetAssetTypeForReference(sourceReference) != assets::ProjectAssetType::Model)
        { if (error) *error = "Exact model publication requires a Library project source."; return false; }
        PreparedModelImportPublication candidate;
        candidate.lock = std::make_unique<assets::ProjectAssetLock>();
        if (!candidate.lock->TryAcquire(project.GetRootDirectory(), error) ||
            !assets::ValidateNoPendingAssetTransactions(project.GetRootDirectory(), error)) return false;
        assets::AssetMetadata metadata;
        assets::ModelGeneratedFile package;
        content::ContentDigest generation;
        if (assets::LoadAssetMetadata(assets::GetAssetMetadataPath(project.ResolveAssetReference(sourceReference)), metadata, error) != assets::AssetMetadataStatus::Success ||
            assets::ReadModelArtifactGeneration(metadata, generation, error) != assets::ModelArtifactGenerationStatus::Success ||
            !assets::ReadActiveModelPackageArtifact(metadata, package, error)) return false;
        if (metadata.id != result.sourceAssetId || generation != result.artifactGenerationKey ||
            package.reference != result.packageArtifact.reference || package.digest != result.packageArtifact.digest)
        { if (error) *error = "The active source publication changed before its import completion was applied."; return false; }
        assets::AssetDatabase database;
        auto scannedProject = project;
        if (!database.Scan(scannedProject, assets::AssetScanOptions{.createMissingMetadata=false, .hashContent=false, .collectDependencies=false}, error)) return false;
        candidate.catalog = database.GetCatalog(); candidate.storage = database.GetStorageMap();
        output = std::move(candidate);
        if (error) error->clear();
        return true;
    }
}
