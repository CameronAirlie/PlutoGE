using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeSurfaceResponse
    {
        public float Friction;
        public fixed byte Sound[4096];
        public fixed byte Particles[4096];
        public fixed byte Decal[4096];
    }

    private static delegate* unmanaged[Cdecl]<uint, int, NativeSurfaceResponse*, int> _resolveSurfaceResponse;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterSurfaceResponseApi")]
    public static int RegisterSurfaceResponseApi(delegate* unmanaged[Cdecl]<uint, int, NativeSurfaceResponse*, int> resolve)
    {
        if (resolve == null || sizeof(NativeSurfaceResponse) != 12292) return 0;
        _resolveSurfaceResponse = resolve;
        return 1;
    }

    internal static bool ResolveSurfaceResponse(uint entity, SurfaceEvent kind, out SurfaceResponse response)
    {
        response = SurfaceResponse.Default;
        if (_resolveSurfaceResponse == null) return false;
        NativeSurfaceResponse native = default;
        if (_resolveSurfaceResponse(entity, (int)kind, &native) == 0) return false;
        response = new(native.Friction,
            Marshal.PtrToStringUTF8((nint)native.Sound) ?? string.Empty,
            Marshal.PtrToStringUTF8((nint)native.Particles) ?? string.Empty,
            Marshal.PtrToStringUTF8((nint)native.Decal) ?? string.Empty);
        return true;
    }
}
