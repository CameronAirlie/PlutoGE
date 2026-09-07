using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Gameplay overlays in the editor Scene and Game views. Colors are RGBA in [0,1].</summary>
public static class DebugDraw
{
    /// <summary>Draw through geometry. Duration zero lasts one host frame; positive durations use simulation seconds.</summary>
    public static bool Line(Vector3 start, Vector3 end, Vector4 color, float duration = 0, string category = "Default") =>
        ScriptBridge.SubmitDebugDraw(0, start, end, color, 1, duration, category ?? "Default", "");

    public static bool Sphere(Vector3 center, float radius, Vector4 color, float duration = 0, string category = "Default") =>
        ScriptBridge.SubmitDebugDraw(1, center, Vector3.Zero, color, radius, duration, category ?? "Default", "");

    public static bool Label(Vector3 position, string text, Vector4 color, float duration = 0, string category = "Default") =>
        ScriptBridge.SubmitDebugDraw(2, position, Vector3.Zero, color, 1, duration, category ?? "Default", text ?? "");

    public static void Clear() => ScriptBridge.ClearDebugDrawing();
}
