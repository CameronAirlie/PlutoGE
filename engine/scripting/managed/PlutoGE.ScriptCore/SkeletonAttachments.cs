using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Attach an existing socket entity to a skinned mesh bone. Put weapon geometry
/// beneath the socket and author its grip offset on that child.</summary>
public static class SkeletonAttachments
{
    /// <summary>Reparents the socket beneath mesh and replaces its transform with the bone pose.
    /// Returns false for missing bones, invalid entities or hierarchy cycles.</summary>
    public static bool Bind(GameObject socket, GameObject mesh, string bone)
    {
        ArgumentNullException.ThrowIfNull(socket);
        ArgumentNullException.ThrowIfNull(mesh);
        var request = new ScriptBridge.NativeSkeletalRequest { Operation = 2, Mesh = mesh.EntityId, Socket = socket.EntityId };
        return ScriptBridge.ControlSkeleton(ref request, root: bone);
    }
    /// <summary>Stops following the bone, retaining the current parent and local pose.</summary>
    public static bool Release(GameObject socket)
    {
        ArgumentNullException.ThrowIfNull(socket);
        var request = new ScriptBridge.NativeSkeletalRequest { Operation = 3, Socket = socket.EntityId };
        return ScriptBridge.ControlSkeleton(ref request);
    }
    public static bool TryGetBoneWorldPose(GameObject mesh, string bone, out Vector3 position, out Quaternion rotation)
    {
        ArgumentNullException.ThrowIfNull(mesh);
        var request = new ScriptBridge.NativeSkeletalRequest { Operation = 6, Mesh = mesh.EntityId };
        var success = ScriptBridge.ControlSkeleton(ref request, root: bone);
        position = success ? request.Position.ToManaged() : Vector3.Zero;
        rotation = success ? request.Rotation.ToManaged() : Quaternion.Identity;
        return success;
    }
}
