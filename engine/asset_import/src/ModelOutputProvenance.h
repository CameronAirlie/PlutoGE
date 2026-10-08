#pragma once
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/ModelImportSettings.h"

namespace PlutoGE::assetimport
{
    bool ValidateModelSourceCorrespondence(const assets::ModelAsset &previous, const assets::ModelImportSettings &settings,
                                           std::string *errorMessage);
    bool ValidateGeneratedFileBaselines(const assets::Project &project, const assets::ModelAsset &previous,
                                        const assets::ModelImportSettings &settings, assets::AssetManager &reader,
                                        std::string *errorMessage, bool disposableStorage = false);
}
