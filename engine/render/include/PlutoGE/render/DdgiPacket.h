#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/DdgiSettings.h"

namespace PlutoGE::render
{
    // DDGI's render packet lanes. Settings round-trip through EncodeDdgiPacket / DecodeDdgiPacket, so the effect, the
    // adapter and the renderer cannot disagree about the layout.
    [[nodiscard]] BasicPostProcessEffect EncodeDdgiPacket(const DdgiSettings &settings, const void *historyOwner);
    [[nodiscard]] DdgiSettings DecodeDdgiPacket(const BasicPostProcessEffect &packet);
}
