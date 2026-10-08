#pragma once

#include "PlutoGE/assets/AssetCatalog.h"
#include <string_view>

namespace PlutoGE::assets
{
    // Versioned storage-neutral catalog encoding. The caller owns IO and atomic
    // publication. Parsing is bounded and preserves the prior catalog on error.
    bool SerializeAssetCatalog(const AssetCatalog &catalog, std::string &text,
                               std::string *errorMessage = nullptr);
    bool ParseAssetCatalog(std::string_view text, AssetCatalog &catalog,
                           std::string *errorMessage = nullptr);
}
