using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeCameraRigRequest
    {
        public int Operation;
        public uint Camera, Target;
        public int Mode, FollowHeading;
        public NativeVector3 Offset, FocusOffset;
        public float Yaw, Pitch, Distance, Smoothing, Radius, Padding, Seconds, Amplitude, Frequency;
    }
    private static delegate* unmanaged[Cdecl]<NativeCameraRigRequest*, int> _controlCameraRig;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterCameraRigApi")]
    public static int RegisterCameraRigApi(delegate* unmanaged[Cdecl]<NativeCameraRigRequest*, int> control)
    {
        if (control == null || sizeof(NativeCameraRigRequest) != 80) return 0;
        _controlCameraRig = control;
        return 1;
    }

    internal static bool ControlCameraRig(NativeCameraRigRequest request) =>
        request.Camera != 0 && _controlCameraRig != null && _controlCameraRig(&request) != 0;
}
