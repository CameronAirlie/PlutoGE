using PlutoGE.ScriptCore.Native;
using System.Numerics;

namespace PlutoGE.ScriptCore;

public sealed class MeshComponent : ComponentReference
{
    internal MeshComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Mesh;

    public bool Static
    {
        get => ScriptBridge.GetMeshStatic(EntityId);
        set => ScriptBridge.SetMeshStatic(EntityId, value);
    }

    /// <summary>Tints every material this mesh draws with (submesh overrides too), as this instance's own copies.</summary>
    public Vector3 Color
    {
        get => ScriptBridge.GetMeshColor(EntityId);
        set => ScriptBridge.SetMeshColor(EntityId, value);
    }

    /// <summary>Emission of every material this mesh draws with, as this instance's own copies.</summary>
    public Vector3 Emission
    {
        get => ScriptBridge.GetMeshEmission(EntityId);
        set => ScriptBridge.SetMeshEmission(EntityId, value);
    }

    /// <summary>
    /// Joints of a skinned mesh hidden from cameras, each with every joint below it: cameras draw the mesh without the
    /// geometry they carry, while its shadow stays whole and bone queries still read the full pose. Names resolve like
    /// bone queries (exact names, then humanoid aliases such as "Head"). A first-person body hides its head this way.
    /// Empty draws the mesh normally. Reads empty, and ignores writes, on hosts without it
    /// (<see cref="SupportsCameraHiddenJoints"/>).
    /// </summary>
    public string[] CameraHiddenJoints
    {
        get => ScriptBridge.GetMeshCameraHiddenJoints(EntityId)
            .Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        set => ScriptBridge.SetMeshCameraHiddenJoints(EntityId, string.Join(",", value ?? Array.Empty<string>()));
    }

    /// <summary>True when the host can hide a mesh's joints from cameras (<see cref="CameraHiddenJoints"/>).</summary>
    public static bool SupportsCameraHiddenJoints => ScriptBridge.IsMeshCameraHiddenJointsSupported;

    /// <summary>True when the host can take script-built geometry (<see cref="SetGeometry"/>).</summary>
    public static bool SupportsGeometry => ScriptBridge.IsMeshGeometrySupported;

    /// <summary>
    /// Replaces the drawn mesh with a script-built triangle list, in the entity's local space. Later calls update it
    /// in place, so it suits geometry rebuilt a few times a second (vision cones, trails). Normals and uvs are
    /// optional (empty spans): missing normals are smoothed from the triangles. No triangles draws nothing. The
    /// component keeps its materials; the geometry is not saved with the scene. Returns false for invalid input or
    /// an older host.
    /// </summary>
    public bool SetGeometry(ReadOnlySpan<Vector3> positions, ReadOnlySpan<uint> indices,
        ReadOnlySpan<Vector3> normals = default, ReadOnlySpan<Vector2> uvs = default)
    {
        if ((!normals.IsEmpty && normals.Length != positions.Length) || (!uvs.IsEmpty && uvs.Length != positions.Length))
        {
            return false;
        }

        return ScriptBridge.SetMeshGeometry(EntityId, positions, normals, uvs, indices);
    }

    /// <summary>Drops script-built geometry and restores the mesh the component had before.</summary>
    public void ClearGeometry() => ScriptBridge.ClearMeshGeometry(EntityId);
}
