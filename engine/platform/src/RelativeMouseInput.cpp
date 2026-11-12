#include "PlutoGE/platform/RelativeMouseInput.h"

#include <cstdlib>
#include <string_view>

#ifdef _WIN32
#include "win32/Win32RawMouseInput.h"
#endif

namespace PlutoGE::platform
{
    std::unique_ptr<RelativeMouseInput> RelativeMouseInput::Create()
    {
        // PLUTO_RAW_MOUSE_THREAD=0 keeps raw motion on the window's own event queue, for comparisons.
        if (const char *setting = std::getenv("PLUTO_RAW_MOUSE_THREAD"); setting && std::string_view(setting) == "0")
            return nullptr;
#ifdef _WIN32
        return CreateWin32RawMouseInput();
#else
        return nullptr;
#endif
    }
}
