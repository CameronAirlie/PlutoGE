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
    pad.connected = true;
    pad.axes.fill(0);
    input.UpdateActiveDevice();
    assert(input.activeDevice == decltype(input.activeDevice)::KeyboardMouse);
    input.BeginFrame();
    pad.axes[0] = 0.05f;
    input.UpdateActiveDevice(); // Drift does not select controller prompts.
    assert(input.activeGamepad == -1);
    pad.buttons[0] = true;
    input.UpdateActiveDevice();
    assert(input.activeGamepad == 2);
    input.BeginFrame();
    input.UpdateActiveDevice(); // Idle retains last device.
    assert(input.activeGamepad == 2);
    input.mouseState.deltaX = 2;
    input.UpdateActiveDevice();
    assert(input.activeGamepad == -1);
    input.BeginFrame();
    pad.buttons[1] = true;
    input.UpdateActiveDevice();
    assert(input.activeGamepad == 2);
    input.BeginFrame();
    pad.connected = false;
    input.UpdateActiveDevice();
    assert(input.activeGamepad == -1);
}
