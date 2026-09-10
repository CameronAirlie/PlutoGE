#include "PlutoGE/platform/InputState.h"
#undef NDEBUG
#include <cassert>

int main()
{
    PlutoGE::platform::InputState input;
    auto &pad = input.gamepads[2];
    pad.connected = true;
    pad.axes[5] = 0.25f;
    assert(input.GetPreviousGamepadAxis(2, 5) == 0.0f);
    input.BeginFrame();
    pad.axes[5] = 0.9f;
    assert(input.GetPreviousGamepadAxis(2, 5) == 0.25f);
    assert(input.GetGamepadAxis(2, 5) == 0.9f);
    input.BeginFrame();
    assert(input.GetPreviousGamepadAxis(2, 5) == 0.9f);
    pad.connected = false;
    assert(input.GetPreviousGamepadAxis(2, 5) == 0.0f);
    assert(input.GetPreviousGamepadAxis(16, 5) == 0.0f);
    assert(input.GetPreviousGamepadAxis(2, 6) == 0.0f);
}
