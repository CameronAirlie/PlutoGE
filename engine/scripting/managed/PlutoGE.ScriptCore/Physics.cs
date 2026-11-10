using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public readonly struct RaycastHit
{
    internal RaycastHit(uint entityId, Vector3 point, Vector3 normal, float distance, ulong foliageInstanceId = 0)
    {
        Entity = new GameObject(entityId);
        Point = point;
        Normal = normal;
        Distance = distance;
        FoliageInstance = new FoliageInstance(entityId, foliageInstanceId);
    }

    internal RaycastHit(in ScriptBridge.NativeRaycastHit native)
        : this(native.EntityId, native.Point.ToManaged(), native.Normal.ToManaged(), native.Distance, native.FoliageInstanceId)
    {
    }

    /// <summary>The entity whose collider was hit. For painted foliage, the entity with the FoliageComponent.</summary>
    public GameObject Entity { get; }
    public Vector3 Point { get; }
    public Vector3 Normal { get; }
    public float Distance { get; }

    /// <summary>
    /// The foliage instance that was hit, when the collider belongs to painted foliage (a tree trunk's capsule);
    /// otherwise <see cref="FoliageInstance.IsValid"/> is false. Promote it to interact with that one plant:
    /// <code>var target = hit.FoliageInstance.TryPromote(out var proxy) ? proxy : hit.Entity;</code>
    /// </summary>
    public FoliageInstance FoliageInstance { get; }
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

    /// <summary>Hits colliders whose entity or any ancestor has the tag. Returns the collider entity.</summary>
    public static bool RaycastTagged(Vector3 origin, Vector3 direction, float maxDistance, string tag, out RaycastHit hit)
    {
        return RaycastTagged(origin, direction, maxDistance, tag, 0, out hit);
    }

    public static bool RaycastTagged(Vector3 origin, Vector3 direction, float maxDistance, string tag, GameObject? ignoredEntity, out RaycastHit hit)
    {
        return RaycastTagged(origin, direction, maxDistance, tag, ignoredEntity?.EntityId ?? 0, out hit);
    }

    /// <summary>Tests an upright capsule against solid colliders, ignoring triggers and the ignored entity hierarchy.
    /// Height includes both caps. With zero displacement this tests penetration; otherwise it also sweeps
    /// to the destination and reports center travel to the first contact. Does not move any entity.</summary>
    public static bool QueryCapsule(Vector3 center, Vector3 displacement, float radius, float height,
                                    GameObject? ignoredEntity, out RaycastHit hit)
    {
        if (!float.IsFinite(radius) || !float.IsFinite(height) || radius <= 0 || height < 2 * radius ||
            !Finite(center) || !Finite(displacement))
            throw new ArgumentOutOfRangeException(nameof(radius), "Capsule dimensions and positions must be finite, with height >= diameter > 0.");
        hit = default;
        if (!ScriptBridge.PhysicsQueryCapsule(center, displacement, radius, height, ignoredEntity?.EntityId ?? 0, out var native))
            return false;
        hit = new RaycastHit(native);
        return true;
    }

    /// <summary>
    /// Every entity whose solid collider overlaps the sphere, each once. Triggers are never included.
    /// <paramref name="tag"/>, when given, keeps only colliders whose entity has that tag.
    /// </summary>
    /// <example><code>foreach (var hit in Physics.OverlapSphere(explosion, 4.0f, tag: "Damageable")) ...</code></example>
    public static GameObject[] OverlapSphere(Vector3 center, float radius, GameObject? ignoredEntity = null, string? tag = null)
    {
        if (!Finite(center) || !float.IsFinite(radius) || radius <= 0.0f)
            return [];
        return ToGameObjects(ScriptBridge.PhysicsOverlapSphere(center, radius, ignoredEntity?.EntityId ?? 0, tag));
    }

    /// <summary>Every entity whose solid collider overlaps the box (half sizes, rotated by <paramref name="rotation"/>).</summary>
    public static GameObject[] OverlapBox(Vector3 center, Vector3 halfExtents, Quaternion rotation, GameObject? ignoredEntity = null,
                                          string? tag = null)
    {
        if (!Finite(center) || !Finite(halfExtents) || halfExtents.X <= 0 || halfExtents.Y <= 0 || halfExtents.Z <= 0)
            return [];
        return ToGameObjects(ScriptBridge.PhysicsOverlapBox(center, halfExtents, rotation, ignoredEntity?.EntityId ?? 0, tag));
    }

    /// <summary>Every solid collider along the ray, nearest first, one hit per collider (per instance for foliage).</summary>
    public static RaycastHit[] RaycastAll(Vector3 origin, Vector3 direction, float maxDistance, GameObject? ignoredEntity = null,
                                          string? tag = null)
    {
        if (!Finite(origin) || direction.LengthSquared() <= 0.000001f || !(maxDistance > 0.0f))
            return [];
        var hits = ScriptBridge.PhysicsRaycastAll(origin, Vector3.Normalize(direction), maxDistance, ignoredEntity?.EntityId ?? 0, tag);
        var result = new RaycastHit[hits.Length];
        for (var index = 0; index < hits.Length; index++)
            result[index] = new RaycastHit(hits[index]);
        return result;
    }

    /// <summary>
    /// Moves a sphere along the ray and reports the first solid collider it touches; <see cref="RaycastHit.Distance"/>
    /// is how far the sphere's centre travelled. Use it for thick projectiles and ledge or step checks.
    /// </summary>
    public static bool SphereCast(Vector3 origin, Vector3 direction, float radius, float maxDistance, out RaycastHit hit,
                                  GameObject? ignoredEntity = null)
    {
        hit = default;
        if (!Finite(origin) || direction.LengthSquared() <= 0.000001f || !(radius > 0.0f) || !(maxDistance > 0.0f))
            return false;
        if (!ScriptBridge.PhysicsSphereCast(origin, direction, radius, maxDistance, ignoredEntity?.EntityId ?? 0, out var native))
            return false;
        hit = new RaycastHit(native);
        return true;
    }

    private static GameObject[] ToGameObjects(uint[] entityIds)
    {
        var objects = new GameObject[entityIds.Length];
        for (var index = 0; index < entityIds.Length; index++)
            objects[index] = new GameObject(entityIds[index]);
        return objects;
    }

    private static bool Finite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

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

        hit = new RaycastHit(nativeHit);
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

        hit = new RaycastHit(nativeHit);
        return true;
    }
}
