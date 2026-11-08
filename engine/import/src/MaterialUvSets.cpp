#include "PlutoGE/import/MaterialUvSets.h"

#include <algorithm>

namespace PlutoGE::assetimport
{
    int SelectPrimaryUvSet(std::span<const SourceTextureUvUse> texturesByPriority)
    {
        const auto first = std::find_if(texturesByPriority.begin(), texturesByPriority.end(),
                                        [](const SourceTextureUvUse &use) { return use.used && use.uvSet >= 0; });
        return first == texturesByPriority.end() ? 0 : first->uvSet;
    }

    EngineUvStream ResolveEngineUvStream(int sourceSet, int primarySet)
    {
        if (sourceSet == primarySet) return EngineUvStream::Primary;
        if (sourceSet == kSecondaryUvSourceSet) return EngineUvStream::Secondary;
        return EngineUvStream::Unavailable;
    }
}
