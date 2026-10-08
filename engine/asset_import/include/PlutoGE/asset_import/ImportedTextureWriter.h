#pragma once

#include <iosfwd>
#include <string>

namespace PlutoGE::assetimport
{
    struct ImportedTextureData;
    // CPU-only serialization. The caller owns output staging and stream closure.
    bool WriteImportedTextureTga(std::ostream &output, const ImportedTextureData &texture,
                                 std::string *errorMessage = nullptr);
}
