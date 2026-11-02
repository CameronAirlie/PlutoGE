#include "LtcTable.h"

#include <glm/gtc/packing.hpp>

namespace PlutoGE::render
{
    namespace
    {
#include "LtcTables.inl"
    }

    LtcTableImage BuildLtcTableImage()
    {
        LtcTableImage image;
        image.width = 2 * LtcTableSize;
        image.height = LtcTableSize;
        image.texels.resize(std::size_t(image.width) * image.height * 4);
        for (int row = 0; row < LtcTableSize; ++row)
            for (int column = 0; column < LtcTableSize; ++column)
            {
                const auto source = std::size_t(row * LtcTableSize + column) * 4;
                for (int half = 0; half < 2; ++half)
                {
                    const float *values = half == 0 ? LtcMatrixTable : LtcAmplitudeTable;
                    const auto target = (std::size_t(row) * image.width + std::size_t(half * LtcTableSize + column)) * 4;
                    for (int channel = 0; channel < 4; ++channel)
                        image.texels[target + channel] = glm::packHalf1x16(values[source + channel]);
                }
            }
        return image;
    }
}
