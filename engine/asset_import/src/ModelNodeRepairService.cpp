#include "PlutoGE/asset_import/ModelNodeRepairService.h"
#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/asset_import/ImportFileTransaction.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include <algorithm>
#include <exception>

namespace PlutoGE::assetimport
{
    namespace
    {
        bool Fail(std::string *error, const char *message)
        { if (error) *error = message; return false; }
        bool ReadProposal(const assets::Project &project, const std::string &sourceReference,
            std::uint64_t incomingNodeId, std::uint64_t retiredNodeId,
            ModelNodeRepairProposal &proposal, assets::AssetMetadata &metadata,
            std::filesystem::path &metadataPath, std::string *error)
        {
            if (project.GetManifest().assetPipelineVersion < 5)
                return Fail(error, "Reviewed node repairs require project format version 5.");
            if (!assets::Project::IsProjectAssetReference(sourceReference) ||
                assets::Project::GetAssetTypeForReference(sourceReference) != assets::ProjectAssetType::Model)
                return Fail(error, "A project model source is required.");
            const auto assetRoot = std::filesystem::canonical(project.GetAssetDirectoryPath());
            const auto source = std::filesystem::canonical(project.ResolveAssetReference(sourceReference));
            if (!content::IsPathWithinDirectory(source, assetRoot, true) ||
                assets::IsAssetInfrastructurePath(project.GetRootDirectory(), source))
                return Fail(error, "The source must remain in the authored asset directory.");
            metadataPath = assets::GetAssetMetadataPath(source);
            if (!content::IsPathWithinDirectory(std::filesystem::canonical(metadataPath), assetRoot, true))
                return Fail(error, "Source metadata resolves outside the authored asset directory.");
            ModelNodeRepairProposal candidate;
            candidate.sourceReference = sourceReference;
            candidate.incomingNodeId = incomingNodeId;
            candidate.retiredNodeId = retiredNodeId;
            if (!content::HashFileContent(metadataPath, candidate.metadataDigest, error) ||
                assets::LoadAssetMetadata(metadataPath, metadata, error) != assets::AssetMetadataStatus::Success ||
                assets::ReadModelArtifactGeneration(metadata, candidate.generation, error) != assets::ModelArtifactGenerationStatus::Success)
                return false;
            ArtifactManifest manifest;
            ArtifactCache cache(project.GetRootDirectory() / "Library" / "Artifacts");
            if (cache.Find(candidate.generation, manifest, error) != ArtifactCacheStatus::Hit ||
                !AreArtifactInputsCurrent(manifest.recipe, error))
                return Fail(error, "Reimport the current model source before reviewing correspondence repair.");
            assets::ModelHierarchyAsset hierarchy;
            if (!assets::LoadModelHierarchyAsset(project, sourceReference, hierarchy, error)) return false;
            // The persisted hierarchy currently stores producer key digests,
            // not original producer identifiers. Never rederive those identities
            // from names when the producer-ID inventory is unavailable.
            if (std::any_of(hierarchy.identities.begin(), hierarchy.identities.end(),
                [](const auto &node) { return node.sourceKey.starts_with("node/source/v1/"); }))
                return Fail(error, "Producer-identified hierarchy repair requires the original persistent producer inventory.");
            std::string hierarchyBytes;
            if (!assets::SerializeModelHierarchyAsset(hierarchy, hierarchyBytes, error)) return false;
            candidate.hierarchyDigest = content::HashContent(std::as_bytes(std::span(hierarchyBytes.data(), hierarchyBytes.size())));
            const auto selected = std::find_if(hierarchy.identities.begin(), hierarchy.identities.end(),
                [&](const auto &node) { return node.localId && node.localId == incomingNodeId; });
            if (selected == hierarchy.identities.end()) return Fail(error, "The reviewed incoming node is not in the current hierarchy.");
            assets::ModelImportSettings settings, repaired;
            std::vector<assets::ModelNodeIdentity> repairedNodes;
            if (assets::ReadModelImportSettings(metadata, settings, error) != assets::ModelImportSettingsStatus::Success ||
                !assets::PrepareModelNodeIdentityRepair(hierarchy.hierarchy, settings,
                    static_cast<std::size_t>(selected - hierarchy.identities.begin()), retiredNodeId, repaired, repairedNodes, error))
                return false;
            for (std::size_t index = 0; index < repairedNodes.size(); ++index)
                if (repairedNodes[index].localId != hierarchy.identities[index].localId) ++candidate.changedNodeCount;
            if (!assets::WriteModelImportSettings(metadata, repaired, error)) return false;
            std::string metadataBytes;
            if (!assets::SerializeAssetMetadata(metadata, metadataBytes, error)) return false;
            candidate.settingsDigest = content::HashContent(std::as_bytes(std::span(metadataBytes.data(), metadataBytes.size())));
            content::ContentDigest current;
            if (!content::HashFileContent(metadataPath, current, error) || current != candidate.metadataDigest ||
                !AreArtifactInputsCurrent(manifest.recipe, error))
                return Fail(error, "Source inputs changed while preparing the repair; review again.");
            proposal = std::move(candidate);
            return true;
        }
    }
    bool ModelNodeRepairService::Prepare(const assets::Project &project, const std::string &sourceReference,
        std::uint64_t incomingNodeId, std::uint64_t retiredNodeId,
        ModelNodeRepairProposal &proposal, std::string *errorMessage) const
    {
        try
        {
            ProjectImportLock lock;
            if (!lock.TryAcquire(project.GetRootDirectory(), errorMessage)) return false;
            assets::AssetMetadata metadata;
            std::filesystem::path path;
            const bool success = ReadProposal(project, sourceReference, incomingNodeId, retiredNodeId, proposal, metadata, path, errorMessage);
            if (success && errorMessage) errorMessage->clear();
            return success;
        }
        catch (const std::exception &error) { if (errorMessage) *errorMessage = error.what(); return false; }
    }
    bool ModelNodeRepairService::Apply(const assets::Project &project, const ModelNodeRepairProposal &proposal,
        std::string *errorMessage) const
    {
        try
        {
            ProjectImportLock lock;
            if (!lock.TryAcquire(project.GetRootDirectory(), errorMessage) ||
                !ImportFileTransaction::Recover(project.GetRootDirectory(), project.GetAssetDirectoryPath(), errorMessage)) return false;
            ModelNodeRepairProposal current;
            assets::AssetMetadata metadata;
            std::filesystem::path path;
            if (!ReadProposal(project, proposal.sourceReference, proposal.incomingNodeId, proposal.retiredNodeId,
                current, metadata, path, errorMessage)) return false;
            if (current.metadataDigest != proposal.metadataDigest || current.generation != proposal.generation ||
                current.hierarchyDigest != proposal.hierarchyDigest || current.settingsDigest != proposal.settingsDigest ||
                current.changedNodeCount != proposal.changedNodeCount)
                return Fail(errorMessage, "The reviewed repair evidence changed; prepare and review again.");
            const auto assetRoot = std::filesystem::canonical(project.GetAssetDirectoryPath());
            const auto relative = path.lexically_relative(assetRoot);
            ImportFileTransaction transaction(project.GetRootDirectory());
            const auto staged = transaction.GetOutputRoot() / relative;
            std::filesystem::create_directories(staged.parent_path());
            if (!assets::SaveAssetMetadata(staged, metadata, assets::AssetMetadataWriteMode::CreateOnly, errorMessage)) return false;
            content::ContentDigest digest;
            if (!content::HashFileContent(path, digest, errorMessage) || digest != proposal.metadataDigest)
                return Fail(errorMessage, "Source metadata changed before publication; review again.");
            ArtifactCache cache(project.GetRootDirectory() / "Library" / "Artifacts");
            ArtifactManifest manifest;
            if (cache.Find(proposal.generation, manifest, errorMessage) != ArtifactCacheStatus::Hit ||
                !AreArtifactInputsCurrent(manifest.recipe, errorMessage))
                return Fail(errorMessage, "Source inputs changed before publication; review again.");
            if (!transaction.Publish(assetRoot, {relative}, errorMessage)) return false;
            if (!AreArtifactInputsCurrent(manifest.recipe, errorMessage))
                return Fail(errorMessage, "Source inputs changed during publication; the repair was rolled back.");
            std::string bytes;
            if (!assets::SerializeAssetMetadata(metadata, bytes, errorMessage) ||
                !content::HashFileContent(path, digest, errorMessage) ||
                digest != content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size()))))
                return Fail(errorMessage, "Published repair metadata failed verification.");
            if (!transaction.Accept(errorMessage)) return false;
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error) { if (errorMessage) *errorMessage = error.what(); return false; }
    }
}
