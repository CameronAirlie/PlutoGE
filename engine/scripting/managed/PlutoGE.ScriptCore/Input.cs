using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum InputDevice { KeyboardMouse, Controller }

public static class Input
{
    /// <summary>Last device with meaningful input; idle connected pads do not select controller prompts.</summary>
    public static InputDevice ActiveDevice => (InputDevice)ScriptBridge.GetActiveInputDevice();
    /// <summary>Logical index of the last active controller, or -1 for keyboard/mouse.</summary>
    public static int ActiveGamepad => ScriptBridge.GetActiveGamepad();
    public static bool UsingController => ActiveDevice == InputDevice.Controller;
    private static InputActionMap? _actionMap;

    public static InputActionMap? ActionMap
    {
        get => _actionMap;
        set => _actionMap = value;
    }

    public static void ApplyActionMap(string assetReference) => _actionMap = InputActionMap.Load(assetReference);
    public static float GetAxis(string action) => _actionMap?.GetAxis(action) ?? 0.0f;
    public static bool GetAction(string action) => _actionMap?.IsDown(action) ?? false;
    public static bool GetActionPressed(string action) => _actionMap?.WasPressed(action) ?? false;

    /// <summary>Timed two-motor output (0..1, up to 5 seconds). Zero duration stops. False when unavailable.</summary>
    public static bool TrySetGamepadVibration(float lowFrequency, float highFrequency, float seconds, int gamepad = 0)
    {
        if (!float.IsFinite(lowFrequency) || lowFrequency < 0 || lowFrequency > 1 ||
            !float.IsFinite(highFrequency) || highFrequency < 0 || highFrequency > 1 ||
            !float.IsFinite(seconds) || seconds < 0 || seconds > 5 || gamepad < 0) return false;
        return ScriptBridge.SetGamepadVibration(gamepad, lowFrequency, highFrequency, seconds);
    }
    public static bool StopGamepadVibration(int gamepad = 0) => TrySetGamepadVibration(0, 0, 0, gamepad);

    public static bool IsGamepadConnected(int gamepad = 0) => ScriptBridge.GetGamepadConnected(gamepad);
    public static bool IsGamepadButtonDown(GamepadButton button, int gamepad = 0) => ScriptBridge.GetGamepadButtonDown(gamepad, (int)button);
    public static bool IsGamepadButtonPressed(GamepadButton button, int gamepad = 0) => ScriptBridge.GetGamepadButtonPressed(gamepad, (int)button);
    public static float GetGamepadAxis(GamepadAxis axis, int gamepad = 0) => ScriptBridge.GetGamepadAxis(gamepad, (int)axis);
    public static bool IsKeyDown(KeyCode key)
    {
        return ScriptBridge.GetKeyDown((int)key);
    }

    public static bool IsKeyPressed(KeyCode key)
    {
        return ScriptBridge.GetKeyPressed((int)key);
    }

    public static bool IsKeyReleased(KeyCode key)
    {
        return ScriptBridge.GetKeyReleased((int)key);
    }

    public static bool IsMouseButtonDown(MouseButton button)
    {
        return ScriptBridge.GetMouseButtonDown((int)button);
    }

    public static bool IsMouseButtonPressed(MouseButton button)
    {
        return ScriptBridge.GetMouseButtonPressed((int)button);
    }

    public static bool IsMouseButtonReleased(MouseButton button)
    {
        return ScriptBridge.GetMouseButtonReleased((int)button);
    }

    public static Vector2 MousePosition => ScriptBridge.GetMousePosition();
    public static Vector2 MouseDelta => ScriptBridge.GetMouseDelta();
    public static Vector2 ScrollDelta => ScriptBridge.GetMouseScrollDelta();
    public static bool QuitRequested => ScriptBridge.GetQuitRequested();

    public static bool CursorLocked
    {
        get => ScriptBridge.GetCursorLocked();
        set => ScriptBridge.SetCursorLocked(value);
    }
}
