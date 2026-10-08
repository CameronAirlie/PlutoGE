#pragma once

#include <filesystem>
#include <string>

namespace PlutoGE::content
{
    // Lexical containment for already resolved absolute paths. No filesystem IO.
    // Windows components use ordinal case-insensitive comparison.
    bool IsPathWithinDirectory(const std::filesystem::path &path, const std::filesystem::path &directory, bool allowEqual = false);

    // Resolve an existing directory prefix and append missing descendants.
    // Performs no writes, rejects existing non-directories, and leaves output
    // unchanged on failure. Avoids Windows weakly_canonical missing-path errors.
    bool ResolveDirectoryForCreation(const std::filesystem::path &requested,
                                     std::filesystem::path &resolved, std::string *errorMessage = nullptr);
}
