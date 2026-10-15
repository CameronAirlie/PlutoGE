using System.Numerics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    private static delegate* unmanaged[Cdecl]<int, int, uint*, int, int> _findEntitiesWithComponent;
    private static delegate* unmanaged[Cdecl]<uint, float*, int> _getEntityWorldMatrix;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterComponentQueryApi")]
    public static int RegisterComponentQueryApi(
        delegate* unmanaged[Cdecl]<int, int, uint*, int, int> findEntitiesWithComponent,
        delegate* unmanaged[Cdecl]<uint, float*, int> getEntityWorldMatrix)
    {
        if (findEntitiesWithComponent == null || getEntityWorldMatrix == null)
        {
            SetError("Managed component query API registration received a null function pointer.");
            return 0;
        }
        _findEntitiesWithComponent = findEntitiesWithComponent;
        _getEntityWorldMatrix = getEntityWorldMatrix;
        _lastError = string.Empty;
        return 1;
    }

    /// <summary>Entity ids in hierarchy order. The native side reports the total so a growing scene is retried.</summary>
    internal static uint[] FindEntitiesWithComponent(NativeComponentType componentType, bool includeInactive)
    {
        if (_findEntitiesWithComponent == null) return [];
        var ids = new uint[64];
        while (true)
        {
            int count;
            fixed (uint* buffer = ids) count = _findEntitiesWithComponent((int)componentType, includeInactive ? 1 : 0, buffer, ids.Length);
            if (count <= ids.Length) return count == ids.Length ? ids : ids[..Math.Max(0, count)];
            ids = new uint[count];
        }
    }

    /// <summary>World transform in System.Numerics row-vector layout (translation in M41..M43).</summary>
    internal static bool TryGetEntityWorldMatrix(uint entityId, out Matrix4x4 matrix)
    {
        matrix = Matrix4x4.Identity;
        if (_getEntityWorldMatrix == null) return false;
        // glm stores columns contiguously; read sequentially that is the transpose numerics expects.
        var values = stackalloc float[16];
        if (_getEntityWorldMatrix(entityId, values) == 0) return false;
        matrix = new Matrix4x4(
            values[0], values[1], values[2], values[3],
            values[4], values[5], values[6], values[7],
            values[8], values[9], values[10], values[11],
            values[12], values[13], values[14], values[15]);
        return true;
    }
}
