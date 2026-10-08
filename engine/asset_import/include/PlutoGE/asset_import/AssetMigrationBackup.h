#pragma once
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/platform/ContentDigest.h"
#include <stop_token>
#include <vector>

namespace PlutoGE::assetimport
{
    struct MigrationBackupFile
    {
        std::filesystem::path projectRelativePath;
        content::ContentDigest contentHash{};
    };
    struct MigrationBackup
    {
        std::filesystem::path directory;
        std::vector<MigrationBackupFile> files;
    };
    // Copies an explicit, hash-bound mutation set under the project lock.
    // This is a recovery copy, not conversion or a complete-project backup.
    // Failed attempts remain marked incomplete; originals are never changed.
    bool CreateAssetMigrationBackup(const assets::Project &project,
        const std::vector<MigrationBackupFile> &files, MigrationBackup &output,
        std::string *errorMessage = nullptr, std::stop_token stop = {});
    // Read-only integrity check. Failure preserves the caller's prior result.
    bool VerifyAssetMigrationBackup(const std::filesystem::path &directory,
        MigrationBackup &output, std::string *errorMessage = nullptr, std::stop_token stop = {});
}
