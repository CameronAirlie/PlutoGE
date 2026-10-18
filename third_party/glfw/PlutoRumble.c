// PlutoGE extension for pinned GLFW 3.4. No guessed XInput slot mapping.
#include "internal.h"
int plutoSetJoystickRumble(int jid, unsigned short low, unsigned short high)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);
#ifdef _GLFW_WIN32
    typedef DWORD (WINAPI *SetStateFn)(DWORD, XINPUT_VIBRATION*);
    _GLFWjoystick* js;
    SetStateFn setState;
    XINPUT_VIBRATION vibration;
    if (jid < 0 || jid > GLFW_JOYSTICK_LAST) return GLFW_FALSE;
    js = &_glfw.joysticks[jid];
    if (!js->allocated || !js->connected || js->win32.device || !_glfw.win32.xinput.instance) return GLFW_FALSE;
    setState = (SetStateFn)GetProcAddress(_glfw.win32.xinput.instance, "XInputSetState");
    if (!setState) return GLFW_FALSE;
    vibration.wLeftMotorSpeed = low;
    vibration.wRightMotorSpeed = high;
    return setState(js->win32.index, &vibration) == ERROR_SUCCESS;
#else
    return GLFW_FALSE;
#endif
}
