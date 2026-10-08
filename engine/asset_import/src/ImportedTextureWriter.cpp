#include "PlutoGE/asset_import/ImportedTextureWriter.h"
#include "PlutoGE/import/MeshImporter.h"
#include <ostream>

namespace PlutoGE::assetimport
{
    bool WriteImportedTextureTga(std::ostream &output, const ImportedTextureData &texture, std::string *errorMessage)
    {
        if (texture.width <= 0 || texture.width > 65535 || texture.height <= 0 || texture.height > 65535 ||
            texture.channels <= 0 || texture.channels > 4 ||
            static_cast<std::uint64_t>(texture.width) * texture.height * texture.channels > texture.pixels.size())
        {
            if (errorMessage)
            {
                *errorMessage = "Imported texture has no pixel data.";
            }
            return false;
        }

        const unsigned char header[18] = {
            0,
            0,
            2,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            static_cast<unsigned char>(texture.width & 0xff),
            static_cast<unsigned char>((texture.width >> 8) & 0xff),
            static_cast<unsigned char>(texture.height & 0xff),
            static_cast<unsigned char>((texture.height >> 8) & 0xff),
            32,
            0x20 | 0x08,
        };
        output.write(reinterpret_cast<const char *>(header), sizeof(header));

        const std::size_t sourceChannels = static_cast<std::size_t>(texture.channels);
        const std::size_t pixelCount = static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height);
        for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
        {
            const std::size_t sourceOffset = pixelIndex * sourceChannels;
            const unsigned char red = texture.pixels[sourceOffset + 0];
            const unsigned char green = sourceChannels > 2 ? texture.pixels[sourceOffset + 1] : red;
            const unsigned char blue = sourceChannels > 2 ? texture.pixels[sourceOffset + 2] : red;
            const unsigned char alpha = sourceChannels == 2 ? texture.pixels[sourceOffset + 1] :
                                        sourceChannels == 4 ? texture.pixels[sourceOffset + 3] : 255;
            const unsigned char bgra[4] = {blue, green, red, alpha};
            output.write(reinterpret_cast<const char *>(bgra), sizeof(bgra));
        }

        if (!output)
        {
            if (errorMessage) *errorMessage = "Cannot write imported texture pixels.";
            return false;
        }
        if (errorMessage) errorMessage->clear();
        return true;
    }

}
