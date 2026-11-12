#pragma once

#include "PlutoGE/platform/RelativeMouseInput.h"

namespace PlutoGE::platform
{
    // Raw mouse reports read on a dedicated thread with GetRawInputBuffer; null if the thread or its raw input
    // registration could not be set up.
    [[nodiscard]] std::unique_ptr<RelativeMouseInput> CreateWin32RawMouseInput();
}
