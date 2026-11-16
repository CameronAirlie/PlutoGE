using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    // Mirrors PlutoGE::scripting::NativeUpscalerRequest (UpscalerScriptApi.h); both sides check the size.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeUpscalerRequest
    {
        public int Operation;
        public int Technology;
        public int Quality;
        public float Sharpness;
        public int Result;
        public int Supported;
        public int Overridden;
        public fixed byte Reason[256];
    }

    internal const int NativeUpscalerRequestSize = 284;

    internal enum UpscalerOperation
    {
        Get = 0,
        Set = 1,
        Reset = 2,
        GetSupport = 3,
    }

    private static delegate* unmanaged[Cdecl]<NativeUpscalerRequest*, int> _controlUpscaler;

    internal static bool UpscalerSupported => _controlUpscaler != null;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterUpscalerApi")]
    public static int RegisterUpscalerApi(delegate* unmanaged[Cdecl]<NativeUpscalerRequest*, int> control)
    {
        if (control == null || sizeof(NativeUpscalerRequest) != NativeUpscalerRequestSize) return 0;
        _controlUpscaler = control;
        return 1;
    }

    // Runs one upscaler request; false when the bridge is not registered or the request was refused.
    internal static bool ControlUpscaler(UpscalerOperation operation, ref NativeUpscalerRequest request)
    {
        if (_controlUpscaler == null) return false;
        request.Operation = (int)operation;
        fixed (NativeUpscalerRequest* pointer = &request)
            return _controlUpscaler(pointer) != 0;
    }

    internal static string UpscalerRequestReason(ref NativeUpscalerRequest request)
    {
        fixed (byte* reason = request.Reason)
            return Marshal.PtrToStringUTF8((nint)reason) ?? string.Empty;
    }
}
