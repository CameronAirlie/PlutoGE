#pragma once

#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    // The preset a camera uses: a searchable dropdown of the project's presets, buttons to edit it in the Post
    // Process Editor or make a new one, and the effects it turns on. Cameras from older builds carry their own
    // inline stack (`effects` with no preset); the field offers to save it as a preset.
    //
    // Returns true when `reference` changed (picked, created or cleared); the caller applies it and marks its
    // document dirty.
    bool DrawPostProcessPresetField(const char *id, std::string &reference,
                                    const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects);
}
