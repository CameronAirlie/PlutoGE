#pragma once

#include "PlutoGE/asset_import/ImportState.h"
#include <unordered_map>

namespace PlutoGE::assetimport
{
    // IO-free reverse import dependencies, separate from runtime references.
    // Paths are absolute normalized input locations; watch adapters resolve aliases.
    class ImportDependencyIndex
    {
    public:
        // Failure retains the previous index. Changed paths return unique, sorted sources.
        bool Replace(const std::vector<ImportState> &states, std::string *errorMessage = nullptr);
        std::vector<std::string> FindAffected(const std::vector<std::filesystem::path> &changedPaths) const;
    private:
        std::unordered_map<std::string, std::vector<std::string>> m_ownersByInput;
    };

    // Read-only rebuild from accepted Library state. Unknown state conservatively
    // schedules its owner; invalid source metadata is reported rather than repaired.
    // Optional coverage is true only when every changed path has a known input owner.
    // On failure both caller outputs remain unchanged.
    bool FindAffectedModelImports(const assets::Project &project, const std::vector<std::filesystem::path> &changedPaths,
                                  std::vector<std::string> &sources, std::string *errorMessage = nullptr,
                                  bool *allPathsIndexed = nullptr);
}
