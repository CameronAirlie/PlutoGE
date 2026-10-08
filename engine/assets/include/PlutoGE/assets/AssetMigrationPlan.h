#pragma once

#include "PlutoGE/assets/AssetMigrationAudit.h"
#include "PlutoGE/platform/ContentDigest.h"

namespace PlutoGE::assets
{
    struct MigrationReferenceMapping
    {
        std::string previousReference;
        std::string logicalReference;
        std::size_t line = 0;
    };
    struct MigrationReferenceFile
    {
        std::string reference;
        content::ContentDigest contentHash{};
        bool imported = false;
        std::vector<MigrationReferenceMapping> mappings;
        std::vector<std::string> diagnostics;
    };
    struct MigrationConfirmedRename
    {
        std::string previousReference;
        std::string replacementReference;
    };
    struct AssetMigrationOptions
    {
        // User-confirmed history only; never inferred from similar filenames.
        std::vector<MigrationConfirmedRename> confirmedRenames;
    };
    struct MigrationRenameEvidence
    {
        MigrationConfirmedRename rename;
        std::string orphanMetadataReference;
        std::string previousAssetId;
        std::string replacementAssetId;
        content::ContentDigest orphanMetadataHash{};
        content::ContentDigest replacementMetadataHash{};
        content::ContentDigest replacementContentHash{};
    };
    struct AssetMigrationPlan
    {
        AssetMigrationAudit audit;
        std::string catalogError;
        std::vector<MigrationReferenceFile> files;
        // Proposed backed-up quarantine, never a filesystem mutation.
        std::vector<MigrationRenameEvidence> renames;
        // Raw audit evidence stays intact; only proven proposed resolutions apply.
        std::vector<std::size_t> resolvedAuditIssues;
        bool HasBlockingIssues() const noexcept;
    };

    // Dry run of identity resolution for the shared scanner's supported formats.
    // Mappings describe occurrences, never textual replacement instructions.
    // Applying them requires format-specific serializers, backups, and a locked
    // revalidation of every input hash. Generated files must be reimported.
    // Conflicting identities produce an audit-only report, never guessed mappings.
    bool PlanAssetReferenceMigration(const Project &project, const AssetMigrationOptions &options, AssetMigrationPlan &output,
                                     std::string *errorMessage = nullptr, std::stop_token stop = {});
    bool PlanAssetReferenceMigration(const Project &project, AssetMigrationPlan &output,
                                     std::string *errorMessage = nullptr, std::stop_token stop = {});
}
