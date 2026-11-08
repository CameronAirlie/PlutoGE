#pragma once

#include <span>

namespace PlutoGE::assetimport
{
    // Imported meshes carry two UV streams: the primary stream, which every material texture samples, and the
    // secondary stream (source set 1), used for lightmaps and by emission when a material opts in. Source formats can
    // author more sets and choose one per texture (glTF texCoord, FBX UV channel). These helpers decide which source
    // set feeds each engine stream so all importers apply the same policy.
    inline constexpr int kSecondaryUvSourceSet = 1;

    struct SourceTextureUvUse
    {
        bool used = false;
        int uvSet = 0;
    };

    // The source set the primary stream reads for one material: the set of the first used texture, in the caller's
    // priority order (base colour first, so the most visible texture is always mapped correctly). 0 when the
    // material samples no texture.
    [[nodiscard]] int SelectPrimaryUvSet(std::span<const SourceTextureUvUse> texturesByPriority);

    enum class EngineUvStream
    {
        Primary,
        Secondary,
        // The texture's set feeds neither stream; it is sampled through the primary stream instead.
        Unavailable,
    };

    // The stream a texture authored against sourceSet is sampled from once the primary stream reads primarySet.
    [[nodiscard]] EngineUvStream ResolveEngineUvStream(int sourceSet, int primarySet);
}
