#pragma once
#include "PlutoGE/assets/ModelInstanceState.h"

namespace PlutoGE::assets
{
    inline constexpr unsigned kLinkedModelSceneProjectVersion = 5;
    // One bounded structural scene record. Binary payload uses canonical lower
    // hex, so scene escaping cannot alter names, references or affine float bits.
    // Version 3 scene readers must reject malformed records rather than skip them.
    bool SerializeSceneModelInstanceRecord(const StaticModelInstanceState &state,
        std::string &record, std::string *errorMessage = nullptr);
    bool ParseSceneModelInstanceRecord(std::string_view record,
        StaticModelInstanceState &state, std::string *errorMessage = nullptr);
}
