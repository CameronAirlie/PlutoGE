namespace PlutoGE.ScriptCore;

// Values intentionally match GLFW's cross-platform standard gamepad layout.
public enum GamepadButton
{
    A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide,
    LeftStick, RightStick, DpadUp, DpadRight, DpadDown, DpadLeft
}

public enum GamepadAxis
{
    LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger
}
