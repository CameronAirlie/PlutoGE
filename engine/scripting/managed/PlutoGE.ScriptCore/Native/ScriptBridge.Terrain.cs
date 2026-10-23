using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    // Mirrors PlutoGE::scripting::NativeTerrainRequest (TerrainScriptApi.h); both sides check the size.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeTerrainRequest
    {
        public int Operation;
        public uint Entity;
        public int Layer;
        public float Radius, Opacity, Hardness;
        public NativeVector3 Position;
        public int Result;
        public float Height;
        public NativeVector3 Normal;
        public fixed float Weights[8];
        public fixed byte Name[128];
    }

    internal enum TerrainOperation
    {
        SampleSurface = 0,
        LayerCount = 1,
        LayerName = 2,
        LayerWeights = 3,
        DominantLayer = 4,
        PaintLayer = 5,
    }

    private static delegate* unmanaged[Cdecl]<NativeTerrainRequest*, int> _controlTerrain;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterTerrainApi")]
    public static int RegisterTerrainApi(delegate* unmanaged[Cdecl]<NativeTerrainRequest*, int> control)
    {
        if (control == null || sizeof(NativeTerrainRequest) != 216) return 0;
        _controlTerrain = control;
        return 1;
    }

    // Runs one terrain request; false when the bridge is not registered or the entity has no terrain.
    internal static bool ControlTerrain(uint entity, TerrainOperation operation, ref NativeTerrainRequest request)
    {
        if (_controlTerrain == null) return false;
        request.Entity = entity;
        request.Operation = (int)operation;
        fixed (NativeTerrainRequest* pointer = &request)
            return _controlTerrain(pointer) != 0;
    }

    internal static string TerrainRequestName(ref NativeTerrainRequest request)
    {
        fixed (byte* name = request.Name)
            return Marshal.PtrToStringUTF8((nint)name) ?? string.Empty;
    }

    internal static float TerrainRequestWeight(ref NativeTerrainRequest request, int layer)
    {
        fixed (float* weights = request.Weights)
            return weights[layer];
    }
}
