#pragma once

#include "PlutoGE/assets/ProjectAssetLock.h"

namespace PlutoGE::assetimport
{
    // Compatibility name; imports and cooks share the same project ownership.
    using ProjectImportLock = assets::ProjectAssetLock;
}
