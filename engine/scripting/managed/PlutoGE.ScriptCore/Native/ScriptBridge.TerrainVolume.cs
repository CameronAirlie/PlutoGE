using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    // Mirrors PlutoGE::scripting::NativeTerrainVolumeRequest (TerrainVolumeScriptApi.h); both sides check the size.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeTerrainVolumeRequest
    {
        public int Operation;
        public uint Entity;
        public int Shape;
        public int Mode;
        public int Material;
        public int Result;
        public float Radius;
        public float MaxExtent;
        public NativeVector3 A;
        public NativeVector3 B;
        public NativeVector3 HalfExtents;
        public NativeQuaternion Rotation;
        public float RockRemoved;
        public float RockAdded;
        public int MainMaterial;
        public int VolumesChanged;
        public int FoliageRemoved;
        public ulong Buffer;
        public long BufferSize;
    }

    internal const int NativeTerrainVolumeRequestSize = 120;

    internal enum TerrainVolumeOperation
    {
        Edit = 0,
        Flush = 1,
        IsUpToDate = 2,
        Save = 3,
        CaptureEdits = 4,
        RestoreEdits = 5,
        ChunkCount = 6,
        ClearEdits = 7,
    }

    private static delegate* unmanaged[Cdecl]<NativeTerrainVolumeRequest*, int> _controlTerrainVolume;

    internal static bool IsTerrainVolumeSupported => _controlTerrainVolume != null;

    /// <summary>Optional: hosts built before terrain volumes never call it, and <see cref="TerrainVolumes.IsSupported"/> is then false.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterTerrainVolumeApi")]
    public static int RegisterTerrainVolumeApi(delegate* unmanaged[Cdecl]<NativeTerrainVolumeRequest*, int> control)
    {
        if (control == null || sizeof(NativeTerrainVolumeRequest) != NativeTerrainVolumeRequestSize)
        {
            SetError("Managed terrain volume API registration received a null function pointer or a mismatched request.");
            return 0;
        }

        _controlTerrainVolume = control;
        _lastError = string.Empty;
        return 1;
    }

    // Runs one terrain volume request; false when the bridge is not registered, the entity has no terrain volume, or
    // the request was malformed.
    internal static bool ControlTerrainVolume(uint entity, TerrainVolumeOperation operation, ref NativeTerrainVolumeRequest request)
    {
        if (_controlTerrainVolume == null) return false;
        request.Entity = entity;
        request.Operation = (int)operation;
        fixed (NativeTerrainVolumeRequest* pointer = &request)
            return _controlTerrainVolume(pointer) != 0;
    }

    // Runs a request over a byte buffer (pinned for the call).
    internal static bool ControlTerrainVolume(uint entity, TerrainVolumeOperation operation, ref NativeTerrainVolumeRequest request, byte[]? buffer)
    {
        if (buffer is null || buffer.Length == 0)
        {
            request.Buffer = 0;
            request.BufferSize = 0;
            return ControlTerrainVolume(entity, operation, ref request);
        }
        fixed (byte* bytes = buffer)
        {
            request.Buffer = (ulong)bytes;
            request.BufferSize = buffer.Length;
            var handled = ControlTerrainVolume(entity, operation, ref request);
            request.Buffer = 0;
            return handled;
        }
    }
}
