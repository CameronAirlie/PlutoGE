using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

[StructLayout(LayoutKind.Sequential)]
internal struct GraphicsQualityNative
{
    public uint ShadowResolution, ShadowCascades;
    public float ShadowDistance;
    public uint Flags;
}

internal static unsafe partial class ScriptBridge
{
    private static delegate* unmanaged[Cdecl]<int, GraphicsQualityNative*, int> _getGraphicsQuality;
    private static delegate* unmanaged[Cdecl]<GraphicsQualityNative*, int> _setGraphicsQuality;
    internal static bool GraphicsQualitySupported => _getGraphicsQuality != null && _setGraphicsQuality != null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterGraphicsQualityApi")]
    public static int RegisterGraphicsQualityApi(delegate* unmanaged[Cdecl]<int, GraphicsQualityNative*, int> get,
        delegate* unmanaged[Cdecl]<GraphicsQualityNative*, int> set)
    {
        if (get == null || set == null) return 0;
        _getGraphicsQuality = get;
        _setGraphicsQuality = set;
        return 1;
    }

    internal static bool TryGetGraphicsQuality(int preset, out GraphicsQualityNative value)
    {
        GraphicsQualityNative result = default;
        bool success = _getGraphicsQuality != null && _getGraphicsQuality(preset, &result) != 0;
        value = result;
        return success;
    }

    internal static bool TrySetGraphicsQuality(GraphicsQualityNative value) =>
        _setGraphicsQuality != null && _setGraphicsQuality(&value) != 0;
}
