using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    // Mirrors PlutoGE::scripting::NativeShatterRequest (FractureScriptApi.h); both sides check the size.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeShatterRequest
    {
        public uint Root;
        public int Pieces;
        public uint Seed;
        public int HasImpact;
        public NativeVector3 ImpactPoint;
        public NativeVector3 ImpactDirection;
        public float ImpactBias;
        public float Density;
        public float BurstSpeed;
        public float ImpactSpeed;
        public float Spin;
        public float MinPieceSize;
    }

    internal const int NativeShatterRequestSize = 64;

    // scene::kMaxShatterPieces: one shatter never spawns more, and the host refuses a smaller buffer.
    internal const int MaxShatterPieces = 1024;

    private static delegate* unmanaged[Cdecl]<NativeShatterRequest*, byte*, uint*, int, int> _shatterEntity;

    internal static bool IsFractureSupported => _shatterEntity != null;

    /// <summary>Optional: hosts built before fracture never call it, and <see cref="Fracture.IsSupported"/> is then false.</summary>
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterFractureApi")]
    public static int RegisterFractureApi(delegate* unmanaged[Cdecl]<NativeShatterRequest*, byte*, uint*, int, int> shatterEntity)
    {
        if (shatterEntity == null || sizeof(NativeShatterRequest) != NativeShatterRequestSize)
        {
            SetError("Managed fracture API registration received a null function pointer or a mismatched request.");
            return 0;
        }

        _shatterEntity = shatterEntity;
        _lastError = string.Empty;
        return 1;
    }

    internal static uint[] ShatterEntity(ref NativeShatterRequest request, string tag)
    {
        if (_shatterEntity == null)
            return [];
        var tagBytes = Encoding.UTF8.GetBytes(tag + '\0');
        var pieces = new uint[MaxShatterPieces];
        int count;
        fixed (NativeShatterRequest* requestPointer = &request)
        fixed (byte* tagPointer = tagBytes)
        fixed (uint* piecePointer = pieces)
            count = _shatterEntity(requestPointer, tagPointer, piecePointer, pieces.Length);
        return pieces[..Math.Clamp(count, 0, pieces.Length)];
    }
}
