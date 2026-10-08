#pragma once

#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/import/MeshImporter.h"

namespace PlutoGE::assetimport
{
    struct ModelSourceSnapshot
    {
        ImportedMeshSourceAsset imported;
        std::vector<ArtifactInput> inputs;
    };

    // Shared by import publication and read-only migration inspection. Color
    // space distinguishes decoded variants of the same source texture.
    std::string GetModelTextureSourceKey(const std::filesystem::path &source,
        const ImportedTextureData &texture, std::size_t index);

    // Discovers dependencies before the authoritative parse, hashes them before
    // and after that parse, and rejects changes to either their set or contents.
    // A future importer IO adapter can supply the same contract in one pass.
    bool ReadModelSourceSnapshot(const std::filesystem::path &source,
                                 const MeshImportOptions &options,
                                 ModelSourceSnapshot &snapshot,
                                 std::string *errorMessage = nullptr);
}
