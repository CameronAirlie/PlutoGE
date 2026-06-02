using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public static class Input
{
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
