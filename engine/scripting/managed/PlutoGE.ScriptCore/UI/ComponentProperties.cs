using System.Globalization;
using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Typed access to any registered component's properties by their serialized names (the generic property bridge).
/// <see cref="RegisteredComponent"/> builds on it, and the typed wrappers with their own bridge calls
/// (RectTransform, UI Text, UI Image) use it for properties those calls do not cover.
/// </summary>
internal static class ComponentProperties
{
    public static string? Get(uint entity, string type, string name) => ScriptBridge.GetComponentProperty(entity, type, name);
    public static bool Set(uint entity, string type, string name, string value) => ScriptBridge.SetComponentProperty(entity, type, name, value);

    public static string GetString(uint entity, string type, string name) => Get(entity, type, name) ?? string.Empty;
    public static void SetString(uint entity, string type, string name, string value) => Set(entity, type, name, value ?? string.Empty);

    public static float GetFloat(uint entity, string type, string name, float fallback = 0.0f) =>
        float.TryParse(Get(entity, type, name), NumberStyles.Float, CultureInfo.InvariantCulture, out var value) ? value : fallback;
    public static void SetFloat(uint entity, string type, string name, float value) =>
        Set(entity, type, name, value.ToString("R", CultureInfo.InvariantCulture));

    public static int GetInt(uint entity, string type, string name, int fallback = 0) =>
        int.TryParse(Get(entity, type, name), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) ? value : fallback;
    public static void SetInt(uint entity, string type, string name, int value) =>
        Set(entity, type, name, value.ToString(CultureInfo.InvariantCulture));

    public static bool GetBool(uint entity, string type, string name) => Get(entity, type, name) is "true" or "1";
    public static void SetBool(uint entity, string type, string name, bool value) => Set(entity, type, name, value ? "true" : "false");

    public static Vector2 GetVector2(uint entity, string type, string name) =>
        ParseVector(Get(entity, type, name), 2) is { } v ? new Vector2(v[0], v[1]) : Vector2.Zero;
    public static void SetVector2(uint entity, string type, string name, Vector2 value) =>
        Set(entity, type, name, FormatVector(value.X, value.Y));

    public static Vector4 GetVector4(uint entity, string type, string name) =>
        ParseVector(Get(entity, type, name), 4) is { } v ? new Vector4(v[0], v[1], v[2], v[3]) : Vector4.Zero;
    public static void SetVector4(uint entity, string type, string name, Vector4 value) =>
        Set(entity, type, name, FormatVector(value.X, value.Y, value.Z, value.W));

    private static float[]? ParseVector(string? text, int count)
    {
        if (string.IsNullOrEmpty(text)) return null;
        var parts = text.Split(',');
        if (parts.Length != count) return null;
        var values = new float[count];
        for (var index = 0; index < count; ++index)
            if (!float.TryParse(parts[index], NumberStyles.Float, CultureInfo.InvariantCulture, out values[index]))
                return null;
        return values;
    }

    private static string FormatVector(params float[] values) =>
        string.Join(",", values.Select(value => value.ToString("R", CultureInfo.InvariantCulture)));
}
