#pragma once

#include "PlutoGE/assets/Project.h"
#include <string>
#include <string_view>

namespace PlutoGE::assetimport
{
    // Moves a project file with its identity sidecar, or a directory with its
    // contents. Never replaces a destination. Cooperates with model imports via
    // the project lock and rolls back ordinary errors. This is not a crash-safe
    // multi-file transaction; callers must report rollback failures for repair.
    bool MoveProjectAsset(const assets::Project &project, std::string_view sourceReference,
                          std::string_view destinationReference, std::string *errorMessage = nullptr);
}
