using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public static class Input
{
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
