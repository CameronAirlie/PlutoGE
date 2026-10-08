#include "PlutoGE/assets/AssetMigrationPlan.h"
#include "AssetMigrationRenames.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/AssetReferences.h"

#include <algorithm>
#include <exception>

namespace PlutoGE::assets
{
    bool AssetMigrationPlan::HasBlockingIssues() const noexcept
    {
        bool auditBlocked = false;
        for (std::size_t index = 0; index < audit.issues.size(); ++index)
            if (audit.issues[index].severity == MigrationIssueSeverity::Error &&
                std::find(resolvedAuditIssues.begin(), resolvedAuditIssues.end(), index) == resolvedAuditIssues.end()) auditBlocked = true;
        return auditBlocked || !catalogError.empty() ||
            std::any_of(files.begin(), files.end(), [](const auto &file) { return !file.diagnostics.empty(); });
    }

    bool PlanAssetReferenceMigration(const Project &project, const AssetMigrationOptions &options, AssetMigrationPlan &output,
                                     std::string *errorMessage, std::stop_token stop)
    {
        auto fail = [&](const std::string &message) { if (errorMessage) *errorMessage = message; return false; };
        try
        {
            AssetMigrationPlan candidate;
            if (!AuditAssetMigration(project, candidate.audit, errorMessage, stop)) return false;
            if (!PlanMigrationRenames(project, options, candidate, errorMessage, stop)) return false;
            if (!candidate.HasBlockingIssues())
            {
                auto inventory = project;
                AssetDatabase database;
                if (database.Scan(inventory, AssetScanOptions{.createMissingMetadata=false, .hashContent=false, .collectDependencies=false}, &candidate.catalogError))
                {
                    std::vector<std::filesystem::path> paths;
                    for (std::filesystem::recursive_directory_iterator it(project.GetAssetDirectoryPath()), end; it != end; ++it)
                    {
                        if (stop.stop_requested()) return fail("Migration planning cancelled.");
                        if (IsAssetInfrastructurePath(project.GetRootDirectory(), it->path()) || it->is_symlink())
                        { it.disable_recursion_pending(); continue; }
                        if (it->is_regular_file() && SupportsAssetReferenceScan(it->path())) paths.push_back(it->path());
                    }
                    std::sort(paths.begin(), paths.end());
                    for (const auto &path : paths)
                    {
                        if (stop.stop_requested()) return fail("Migration planning cancelled.");
                        MigrationReferenceFile file;
                        file.reference = project.MakeAssetReference(path);
                        if (!content::HashFileContent(path, file.contentHash, errorMessage)) return false;
                        const auto identity = database.GetIdentityForReference(file.reference);
                        const auto *descriptor = identity ? database.GetCatalog()->Find(*identity) : nullptr;
                        file.imported = descriptor && descriptor->ownership == AssetOwnership::Imported;
                        auto scan = ScanAssetReferences(path, stop, project.GetAssetDirectoryPath());
                        if (scan.cancelled) return fail("Migration planning cancelled.");
                        file.diagnostics = std::move(scan.errors);
                        file.imported = file.imported || std::any_of(scan.occurrences.begin(), scan.occurrences.end(),
                            [](const auto &occurrence) { return occurrence.role == AssetReferenceRole::ImportSource; });
                        for (const auto &occurrence : scan.occurrences)
                        {
                            if (occurrence.role != AssetReferenceRole::Runtime || Project::IsEngineAssetReference(occurrence.reference)) continue;
                            AssetReference logical;
                            if (ParseAssetReference(occurrence.reference, logical))
                            {
                                if (std::any_of(candidate.renames.begin(), candidate.renames.end(), [&](const auto &evidence)
                                    { return evidence.previousAssetId == logical.assetId && evidence.previousAssetId != evidence.replacementAssetId; }))
                                    file.diagnostics.push_back("Logical identity overlaps a confirmed renamed source; review this occurrence explicitly: " + occurrence.reference);
                                else if (!database.GetCatalog()->Find(logical)) file.diagnostics.push_back("Unresolved logical reference: " + occurrence.reference);
                                continue;
                            }
                            if (!Project::IsProjectAssetReference(occurrence.reference))
                            {
                                file.diagnostics.push_back("Reference requires a compatibility resolver: " + occurrence.reference);
                                continue;
                            }
                            const auto target = database.GetIdentityForReference(ResolveMigrationRename(project, candidate, occurrence.reference));
                            if (!target)
                            {
                                file.diagnostics.push_back("Missing or ambiguous persisted identity: " + occurrence.reference);
                                continue;
                            }
                            MigrationReferenceMapping mapping{occurrence.reference, {}, occurrence.line};
                            if (!SerializeAssetReference(*target, mapping.logicalReference, errorMessage)) return false;
                            file.mappings.push_back(std::move(mapping));
                        }
                        content::ContentDigest after;
                        if (!content::HashFileContent(path, after, errorMessage)) return false;
                        if (after != file.contentHash) return fail("Asset changed during migration planning: " + path.string());
                        candidate.files.push_back(std::move(file));
                    }
                }
            }
            if (stop.stop_requested()) return fail("Migration planning cancelled.");
            for (const auto &evidence : candidate.renames)
            {
                content::ContentDigest hash;
                if (!content::HashFileContent(project.ResolveAssetReference(evidence.orphanMetadataReference), hash, errorMessage) || hash != evidence.orphanMetadataHash ||
                    !content::HashFileContent(GetAssetMetadataPath(project.ResolveAssetReference(evidence.rename.replacementReference)), hash, errorMessage) || hash != evidence.replacementMetadataHash ||
                    !content::HashFileContent(project.ResolveAssetReference(evidence.rename.replacementReference), hash, errorMessage) || hash != evidence.replacementContentHash)
                    return fail("Confirmed rename inputs changed during migration planning.");
                std::error_code error;
                const auto old = std::filesystem::symlink_status(project.ResolveAssetReference(evidence.rename.previousReference), error);
                if ((error && error != std::errc::no_such_file_or_directory) || old.type() != std::filesystem::file_type::not_found)
                    return fail("Old source appeared during migration planning.");
            }
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception) { return fail(std::string("Cannot plan asset reference migration: ") + exception.what()); }
    }
    bool PlanAssetReferenceMigration(const Project &project, AssetMigrationPlan &output,
                                     std::string *errorMessage, std::stop_token stop)
    { return PlanAssetReferenceMigration(project, AssetMigrationOptions{}, output, errorMessage, stop); }
}
