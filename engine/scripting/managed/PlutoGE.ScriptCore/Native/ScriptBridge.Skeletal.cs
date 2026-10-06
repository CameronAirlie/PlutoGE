using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeSkeletalRequest
    {
        public int Operation;
        public uint Owner, Mesh, Socket;
        public NativeVector3 Position, Pole;
        public NativeQuaternion Rotation;
        public float Weight, RotationWeight;
    }
    private static delegate* unmanaged[Cdecl]<NativeSkeletalRequest*, byte*, byte*, byte*, byte*, int> _controlSkeleton;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterSkeletalApi")]
    public static int RegisterSkeletalApi(delegate* unmanaged[Cdecl]<NativeSkeletalRequest*, byte*, byte*, byte*, byte*, int> control)
    {
        if (control == null || sizeof(NativeSkeletalRequest) != 64) return 0;
        _controlSkeleton = control;
        return 1;
    }
    internal static bool ControlSkeleton(ref NativeSkeletalRequest request, string id = "", string root = "", string middle = "", string tip = "")
    {
        if (_controlSkeleton == null) return false;
        foreach (var value in new[] { id, root, middle, tip })
            if (value is null || value.Contains('\0') || Encoding.UTF8.GetByteCount(value) > 256) return false;
        byte[] a = Encoding.UTF8.GetBytes(id + '\0'), b = Encoding.UTF8.GetBytes(root + '\0'),
               c = Encoding.UTF8.GetBytes(middle + '\0'), d = Encoding.UTF8.GetBytes(tip + '\0');
        fixed (NativeSkeletalRequest* r = &request)
        fixed (byte* pa = a, pb = b, pc = c, pd = d)
            return _controlSkeleton(r, pa, pb, pc, pd) != 0;
    }
}
