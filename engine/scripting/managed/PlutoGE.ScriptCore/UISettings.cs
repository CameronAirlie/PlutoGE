using PlutoGE.ScriptCore.Native;
namespace PlutoGE.ScriptCore;

/// <summary>Screen-space RmlUi layout preferences. Use dp for scalable sizes and physical/viewport units for projected geometry.</summary>
public static class UISettings
{
    public static bool IsSupported => ScriptBridge.UISettingsSupported;
    public static float InterfaceScale => ScriptBridge.GetInterfaceScale();
    /// <summary>Sets the screen context dp ratio (0.5–3). Does not resize world UI, px, percentages, or viewport units.</summary>
    public static bool TrySetInterfaceScale(float scale)
    {
        if (!float.IsFinite(scale) || scale < .5f || scale > 3) throw new ArgumentOutOfRangeException(nameof(scale));
        return ScriptBridge.SetInterfaceScale(scale);
    }
}
