#pragma once

#include "PlutoGE/assets/Project.h"
#include <cstddef>
#include <stop_token>
#include <string>
#include <vector>

namespace PlutoGE::assets
{
    enum class MigrationIssueSeverity { Warning, Error };
    enum class MigrationIssueKind { MissingMetadata, InvalidMetadata, OrphanMetadata, DuplicateIdentity, SymbolicLink };
    struct MigrationAuditIssue
    {
        MigrationIssueSeverity severity = MigrationIssueSeverity::Warning;
        MigrationIssueKind kind = MigrationIssueKind::MissingMetadata;
        std::string reference;
        std::string relatedReference;
        std::string message;
    };
    struct AssetMigrationAudit
    {
        std::size_t assets = 0;
        std::size_t metadataFiles = 0;
        std::size_t identifiedAssets = 0;
        std::vector<MigrationAuditIssue> issues;
        bool HasBlockingIssues() const noexcept;
    };

    // Advisory inventory only: never repairs metadata, acquires a creating lock,
    // recovers transactions, or changes the project manifest. Includes orphan
    // sidecars in duplicate detection. It is not authorization to convert a
    // project; conversion must repeat validation under its publication lock.
    // Enumeration/cancellation failures leave output unchanged.
    bool AuditAssetMigration(const Project &project, AssetMigrationAudit &output,
                             std::string *errorMessage = nullptr, std::stop_token stop = {});
}
