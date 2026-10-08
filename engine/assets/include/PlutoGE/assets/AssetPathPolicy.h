#pragma once

#include <filesystem>
#include <string>

namespace PlutoGE::assets
{
    // Lexical policy for paths expressed against the same normalized project
    // root (no filesystem IO or symlink resolution). Scanners should disable
    // recursion when this returns true for the current entry.
    // Read-only guard for consumers that cannot recover importer journals.
    bool ValidateNoPendingAssetTransactions(const std::filesystem::path &projectRoot, std::string *errorMessage = nullptr);
    bool IsAssetInfrastructurePath(const std::filesystem::path &projectRoot, const std::filesystem::path &path);
}
