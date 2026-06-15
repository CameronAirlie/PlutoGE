using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public readonly struct RaycastHit
{
    internal RaycastHit(uint entityId, Vector3 point, Vector3 normal, float distance)
    {
        Entity = new GameObject(entityId);
        Point = point;
        Normal = normal;
        Distance = distance;
    }

    public GameObject Entity { get; }
    public Vector3 Point { get; }
    public Vector3 Normal { get; }
    public float Distance { get; }
}

public static class Physics
{
    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, out RaycastHit hit)
    {
        return Raycast(origin, direction, maxDistance, 0, out hit);
    }

    public static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, GameObject? ignoredEntity, out RaycastHit hit)
    {
        return Raycast(origin, direction, maxDistance, ignoredEntity?.EntityId ?? 0, out hit);
    }

    public static bool RaycastTagged(Vector3 origin, Vector3 direction, float maxDistance, string tag, out RaycastHit hit)
    {
        return RaycastTagged(origin, direction, maxDistance, tag, 0, out hit);
    }

    public static bool RaycastTagged(Vector3 origin, Vector3 direction, float maxDistance, string tag, GameObject? ignoredEntity, out RaycastHit hit)
    {
        return RaycastTagged(origin, direction, maxDistance, tag, ignoredEntity?.EntityId ?? 0, out hit);
    }

    public static Vector3 MoveKinematic(GameObject gameObject, Vector3 displacement, float skinWidth = 0.02f)
    {
        return gameObject.IsValid
            ? ScriptBridge.PhysicsMoveKinematic(gameObject.EntityId, displacement, skinWidth)
            : Vector3.Zero;
    }

    public static Vector3 MoveKinematic(uint entityId, Vector3 displacement, float skinWidth = 0.02f)
    {
        return entityId != 0
            ? ScriptBridge.PhysicsMoveKinematic(entityId, displacement, skinWidth)
            : Vector3.Zero;
    }

    private static bool Raycast(Vector3 origin, Vector3 direction, float maxDistance, uint ignoredEntityId, out RaycastHit hit)
    {
        hit = default;
        if (direction.LengthSquared() <= 0.000001f || maxDistance <= 0.0f)
        {
            return false;
        }

        if (!ScriptBridge.PhysicsRaycast(origin, Vector3.Normalize(direction), maxDistance, ignoredEntityId, out var nativeHit))
        {
            return false;
        }

        hit = new RaycastHit(
            nativeHit.EntityId,
            nativeHit.Point.ToManaged(),
            nativeHit.Normal.ToManaged(),
            nativeHit.Distance);
        return true;
    }

    private static bool RaycastTagged(Vector3 origin, Vector3 direction, float maxDistance, string tag, uint ignoredEntityId, out RaycastHit hit)
    {
        hit = default;
        if (direction.LengthSquared() <= 0.000001f || maxDistance <= 0.0f)
        {
            return false;
        }

        if (!ScriptBridge.PhysicsRaycastTagged(origin, Vector3.Normalize(direction), maxDistance, ignoredEntityId, tag, out var nativeHit))
        {
            return false;
        }

        hit = new RaycastHit(
            nativeHit.EntityId,
            nativeHit.Point.ToManaged(),
            nativeHit.Normal.ToManaged(),
            nativeHit.Distance);
        return true;
    }
}
