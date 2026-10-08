#include "AssetMigrationRenames.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include <algorithm>
#include <map>
#include <fstream>
#include <set>
#include <stdexcept>

namespace PlutoGE::assets
{
    namespace
    {
        std::filesystem::path Location(const Project &project, std::string_view reference)
        {
            if (!Project::IsProjectAssetReference(reference) || reference.find_first_of("\t\r\n\0", 0, 4) != std::string_view::npos)
                throw std::runtime_error("Confirmed rename requires project-relative references.");
            const auto relative = std::filesystem::u8path(reference.substr(Project::kProjectAssetScheme.size()));
            if (relative.empty() || relative.has_root_path()) throw std::runtime_error("Invalid confirmed rename location.");
            for (const auto &part : relative) if (part == "." || part == "..") throw std::runtime_error("Confirmed rename cannot contain traversal segments.");
            const auto root = std::filesystem::canonical(project.GetAssetDirectoryPath());
            const auto path = root / relative;
            std::filesystem::path parent;
            std::string error;
            if (IsAssetInfrastructurePath(project.GetRootDirectory(), path) ||
                !content::ResolveDirectoryForCreation(path.parent_path(), parent, &error) ||
                !content::IsPathWithinDirectory(parent, root, true)) throw std::runtime_error("Confirmed rename escapes the authored asset root.");
            return parent / path.filename();
        }
        bool Same(const std::filesystem::path &a, const std::filesystem::path &b)
        { return content::IsPathWithinDirectory(a, b, true) && content::IsPathWithinDirectory(b, a, true); }
        bool Absent(const std::filesystem::path &path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            return (error == std::errc::no_such_file_or_directory || !error) && status.type() == std::filesystem::file_type::not_found;
        }
        AssetMetadata Metadata(const std::filesystem::path &path, content::ContentDigest &hash)
        {
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path))) throw std::runtime_error("Confirmed rename metadata must be an ordinary file.");
            const auto size = std::filesystem::file_size(path);
            if (size > 1024 * 1024) throw std::runtime_error("Rename metadata exceeds its size limit.");
            std::string bytes(static_cast<std::size_t>(size), '\0');
            std::ifstream input(path, std::ios::binary);
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!input || input.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Cannot read rename metadata coherently.");
            hash = content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size())));
            AssetMetadata metadata;
            std::string error;
            if (ParseAssetMetadata(bytes, metadata, &error) != AssetMetadataStatus::Success) throw std::runtime_error(error);
            content::ContentDigest after;
            if (!content::HashFileContent(path, after, &error) || after != hash) throw std::runtime_error("Rename metadata changed during migration planning.");
            return metadata;
        }
    }

    bool PlanMigrationRenames(const Project &project, const AssetMigrationOptions &options,
        AssetMigrationPlan &candidate, std::string *errorMessage, std::stop_token stop)
    {
        try
        {
            if (options.confirmedRenames.empty()) return true;
            if (options.confirmedRenames.size() > 100) throw std::runtime_error("Too many confirmed source renames.");
            std::set<std::string> quarantine;
            for (const auto &rename : options.confirmedRenames)
            {
                if (stop.stop_requested()) throw std::runtime_error("Migration planning cancelled.");
                const auto previous = Location(project, rename.previousReference);
                const auto replacement = Location(project, rename.replacementReference);
                if (Same(previous, replacement) || !Absent(previous) ||
                    Project::GetAssetTypeForReference(rename.previousReference) != ProjectAssetType::Model ||
                    Project::GetAssetTypeForReference(rename.replacementReference) != ProjectAssetType::Model ||
                    !std::filesystem::is_regular_file(std::filesystem::symlink_status(replacement)))
                    throw std::runtime_error("Confirmed rename requires a missing model source and an existing ordinary replacement model.");
                for (const auto &existing : candidate.renames)
                    if (Same(previous, Location(project, existing.rename.previousReference))) throw std::runtime_error("Duplicate confirmed rename origin.");
                MigrationRenameEvidence evidence;
                evidence.rename = rename;
                evidence.orphanMetadataReference = rename.previousReference + ".plutometa";
                const auto orphan = Metadata(GetAssetMetadataPath(previous), evidence.orphanMetadataHash);
                const auto current = Metadata(GetAssetMetadataPath(replacement), evidence.replacementMetadataHash);
                evidence.previousAssetId = orphan.id; evidence.replacementAssetId = current.id;
                std::string error;
                if (!content::HashFileContent(replacement, evidence.replacementContentHash, &error)) throw std::runtime_error(error);
                quarantine.insert(evidence.orphanMetadataReference);
                candidate.renames.push_back(std::move(evidence));
            }
            // Evaluate complete duplicate families after proposed quarantine.
            // Removing one orphan must never hide two remaining active owners.
            std::map<std::string, std::set<std::string>> families;
            std::map<std::size_t, std::string> issueOwners;
            for (std::size_t index = 0; index < candidate.audit.issues.size(); ++index)
            {
                if (stop.stop_requested()) throw std::runtime_error("Migration planning cancelled.");
                const auto &issue = candidate.audit.issues[index];
                if (issue.kind == MigrationIssueKind::OrphanMetadata && quarantine.contains(issue.reference))
                    candidate.resolvedAuditIssues.push_back(index);
                if (issue.kind != MigrationIssueKind::DuplicateIdentity) continue;
                content::ContentDigest ignored;
                const auto metadata = Metadata(Location(project, issue.reference), ignored);
                const auto related = Metadata(Location(project, issue.relatedReference), ignored);
                if (metadata.id != related.id) throw std::runtime_error("Duplicate identity inventory changed during migration planning.");
                families[metadata.id].insert(issue.reference); families[metadata.id].insert(issue.relatedReference);
                issueOwners[index] = metadata.id;
            }
            for (const auto &[index, owner] : issueOwners)
            {
                const auto &family = families.at(owner);
                const auto remaining = std::count_if(family.begin(), family.end(), [&](const auto &reference) { return !quarantine.contains(reference); });
                if (remaining <= 1) candidate.resolvedAuditIssues.push_back(index);
            }
            if (errorMessage) errorMessage->clear(); return true;
        }
        catch (const std::exception &error) { if (errorMessage) *errorMessage = error.what(); return false; }
    }

    std::string ResolveMigrationRename(const Project &project, const AssetMigrationPlan &plan, const std::string &reference)
    {
        if (plan.renames.empty()) return reference;
        std::filesystem::path path;
        try { path = Location(project, reference); }
        catch (const std::exception &) { return reference; }
        for (const auto &evidence : plan.renames)
            if (Same(path, Location(project, evidence.rename.previousReference))) return evidence.rename.replacementReference;
        return reference;
    }
}
