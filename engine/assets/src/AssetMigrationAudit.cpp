#include "PlutoGE/assets/AssetMigrationAudit.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetPathPolicy.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>

namespace PlutoGE::assets
{
    bool AssetMigrationAudit::HasBlockingIssues() const noexcept
    {
        return std::any_of(issues.begin(), issues.end(), [](const auto &issue)
            { return issue.severity == MigrationIssueSeverity::Error; });
    }

    bool AuditAssetMigration(const Project &project, AssetMigrationAudit &output,
                             std::string *errorMessage, std::stop_token stop)
    {
        auto fail = [&](const std::string &message) { if (errorMessage) *errorMessage = message; return false; };
        try
        {
            if (stop.stop_requested()) return fail("Migration audit cancelled.");
            if (!ValidateNoPendingAssetTransactions(project.GetRootDirectory(), errorMessage)) return false;
            AssetMigrationAudit candidate;
            std::vector<std::filesystem::path> paths;
            std::set<std::filesystem::path> files;
            for (std::filesystem::recursive_directory_iterator it(project.GetAssetDirectoryPath()), end; it != end; ++it)
            {
                if (stop.stop_requested()) return fail("Migration audit cancelled.");
                if (IsAssetInfrastructurePath(project.GetRootDirectory(), it->path()))
                { it.disable_recursion_pending(); continue; }
                if (it->is_symlink())
                {
                    it.disable_recursion_pending();
                    candidate.issues.push_back({MigrationIssueSeverity::Warning, MigrationIssueKind::SymbolicLink,
                        project.MakeAssetReference(it->path()), {}, "Symbolic link excluded from migration inventory."});
                    continue;
                }
                if (it->is_regular_file()) { paths.push_back(it->path()); files.insert(it->path()); }
            }
            std::sort(paths.begin(), paths.end());
            std::map<std::string, std::string> owners;
            for (const auto &path : paths)
            {
                if (stop.stop_requested()) return fail("Migration audit cancelled.");
                const auto reference = project.MakeAssetReference(path);
                if (path.extension() == ".plutometa")
                {
                    ++candidate.metadataFiles;
                    auto assetPath = path;
                    assetPath.replace_extension();
                    const bool orphan = !files.contains(assetPath);
                    if (orphan)
                        candidate.issues.push_back({MigrationIssueSeverity::Warning, MigrationIssueKind::OrphanMetadata,
                            reference, {}, "Metadata has no ordinary asset file; preserve it until its origin is reviewed."});
                    AssetMetadata metadata;
                    std::string reason;
                    if (LoadAssetMetadata(path, metadata, &reason) != AssetMetadataStatus::Success)
                    {
                        candidate.issues.push_back({MigrationIssueSeverity::Error, MigrationIssueKind::InvalidMetadata,
                            reference, {}, reason});
                        continue;
                    }
                    if (!orphan) ++candidate.identifiedAssets;
                    const auto [previous, inserted] = owners.emplace(metadata.id, reference);
                    if (!inserted)
                        candidate.issues.push_back({MigrationIssueSeverity::Error, MigrationIssueKind::DuplicateIdentity,
                            reference, previous->second, "Duplicate persistent asset ID: " + metadata.id});
                    continue;
                }
                if (Project::GetAssetTypeForReference(reference) == ProjectAssetType::Unknown && path.extension() != ".plutomodel") continue;
                ++candidate.assets;
                if (!files.contains(GetAssetMetadataPath(path)))
                    candidate.issues.push_back({MigrationIssueSeverity::Warning, MigrationIssueKind::MissingMetadata,
                        reference, {}, "Asset has no persisted identity; migration must assign one before rewriting references."});
            }
            std::sort(candidate.issues.begin(), candidate.issues.end(), [](const auto &a, const auto &b)
            {
                if (a.reference != b.reference) return a.reference < b.reference;
                return a.kind < b.kind;
            });
            if (stop.stop_requested()) return fail("Migration audit cancelled.");
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception) { return fail(std::string("Cannot audit project assets: ") + exception.what()); }
    }
}
