using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeSceneStreamingRequest
    {
        public ulong Id, Generation;
        public int Operation, State;
        public float Progress;
        public nint Path, Error;
    }
    private static delegate* unmanaged[Cdecl]<NativeSceneStreamingRequest*, int> _sceneStreaming;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterSceneStreamingApi")]
    public static int RegisterSceneStreamingApi(delegate* unmanaged[Cdecl]<NativeSceneStreamingRequest*, int> control)
    {
        if (control == null || sizeof(NativeSceneStreamingRequest) != 48) return 0;
        _sceneStreaming = control;
        return 1;
    }
    internal static bool SceneStreaming(ref NativeSceneStreamingRequest request, string? path, out string error)
    {
        error = "Scene streaming API is unavailable.";
        if (_sceneStreaming == null) return false;
        nint utf8 = path is null ? 0 : Marshal.StringToCoTaskMemUTF8(path);
        try
        {
            request.Path = utf8;
            request.Error = 0;
            fixed (NativeSceneStreamingRequest* pointer = &request)
            {
                bool success = _sceneStreaming(pointer) != 0;
                error = Marshal.PtrToStringUTF8(request.Error) ?? string.Empty;
                return success;
            }
        }
        finally { Marshal.FreeCoTaskMem(utf8); request.Path = 0; request.Error = 0; }
    }
}
