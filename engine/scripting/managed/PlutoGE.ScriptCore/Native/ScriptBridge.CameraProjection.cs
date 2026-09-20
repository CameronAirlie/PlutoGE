using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    private static delegate* unmanaged[Cdecl]<uint, int> _getCameraProjection;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setCameraProjection;
    private static delegate* unmanaged[Cdecl]<uint, float> _getCameraOrthographicHeight;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setCameraOrthographicHeight;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterCameraProjectionApi")]
    public static int RegisterCameraProjectionApi(
        delegate* unmanaged[Cdecl]<uint, int> getProjection,
        delegate* unmanaged[Cdecl]<uint, int, void> setProjection,
        delegate* unmanaged[Cdecl]<uint, float> getHeight,
        delegate* unmanaged[Cdecl]<uint, float, void> setHeight)
    {
        if (getProjection == null || setProjection == null || getHeight == null || setHeight == null)
        {
            SetError("Managed camera projection API registration received a null function pointer.");
            return 0;
        }
        _getCameraProjection = getProjection;
        _setCameraProjection = setProjection;
        _getCameraOrthographicHeight = getHeight;
        _setCameraOrthographicHeight = setHeight;
        _lastError = string.Empty;
        return 1;
    }

    internal static CameraProjection GetCameraProjection(uint id) =>
        _getCameraProjection == null ? CameraProjection.Perspective : (CameraProjection)_getCameraProjection(id);
    internal static void SetCameraProjection(uint id, CameraProjection value)
    {
        if (_setCameraProjection != null) _setCameraProjection(id, (int)value);
    }
    internal static float GetCameraOrthographicHeight(uint id) =>
        _getCameraOrthographicHeight == null ? 10.0f : _getCameraOrthographicHeight(id);
    internal static void SetCameraOrthographicHeight(uint id, float value)
    {
        if (_setCameraOrthographicHeight != null) _setCameraOrthographicHeight(id, value);
    }
}
