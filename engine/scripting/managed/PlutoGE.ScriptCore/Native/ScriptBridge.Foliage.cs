using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    // Mirrors PlutoGE::scripting::NativeFoliageRequest (FoliageScriptApi.h); both sides check the size.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeFoliageRequest
    {
        public int Operation;
        public uint Entity;
        public ulong InstanceId;
        public NativeVector3 Position;
        public float Radius;
        public NativeQuaternion Rotation;
        public NativeVector3 Scale;
        public int Result;
        public int TypeIndex;
        public uint LinkedEntity;
        public int Capacity;
        public int HasProxyPrefab;
        public ulong* Ids;
        public fixed byte Name[256];
    }

    internal enum FoliageOperation
    {
        GetInstance = 0,
        Promote = 1,
        Release = 2,
        FindInRadius = 3,
        GetProxyLink = 4,
    }

    private static delegate* unmanaged[Cdecl]<NativeFoliageRequest*, int> _controlFoliage;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterFoliageApi")]
    public static int RegisterFoliageApi(delegate* unmanaged[Cdecl]<NativeFoliageRequest*, int> control)
    {
        if (control == null || sizeof(NativeFoliageRequest) != 344 || sizeof(NativeRaycastHit) != 40) return 0;
        _controlFoliage = control;
        return 1;
    }

    // Runs one foliage request; false when the bridge is not registered or the entity lacks the component involved.
    internal static bool ControlFoliage(uint entity, FoliageOperation operation, ref NativeFoliageRequest request)
    {
        if (_controlFoliage == null || entity == 0) return false;
        request.Entity = entity;
        request.Operation = (int)operation;
        fixed (NativeFoliageRequest* pointer = &request)
            return _controlFoliage(pointer) != 0;
    }

    internal static string FoliageRequestName(ref NativeFoliageRequest request)
    {
        fixed (byte* name = request.Name)
            return Marshal.PtrToStringUTF8((nint)name) ?? string.Empty;
    }

    // The IDs of the instances whose pivot is within `radius` of `center`, nearest first.
    internal static ulong[] FoliageFindInRadius(uint entity, NativeVector3 center, float radius)
    {
        var ids = new ulong[32];
        for (var attempt = 0; attempt < 2; attempt++)
        {
            fixed (ulong* buffer = ids)
            {
                var request = new NativeFoliageRequest { Position = center, Radius = radius, Capacity = ids.Length, Ids = buffer };
                if (!ControlFoliage(entity, FoliageOperation.FindInRadius, ref request)) return [];
                if (request.Result <= ids.Length) return ids[..request.Result];
                ids = new ulong[request.Result];
            }
        }
        return ids;
    }
}
