using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Painted foliage: many copies of a few plants, drawn and collided with as instances rather than entities. Scripts
/// find instances (by a ray's <see cref="RaycastHit.FoliageInstance"/>, or near a point) and promote the ones gameplay
/// needs into full entities. See <see cref="FoliageInstance"/> and docs/FOLIAGE.md, "Interactive foliage".
/// </summary>
public sealed class FoliageComponent : ComponentReference
{
    internal FoliageComponent(uint entityId) : base(entityId) {}

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Foliage;

    /// <summary>The instance with this ID; check <see cref="FoliageInstance.Exists"/> before relying on it.</summary>
    public FoliageInstance GetInstance(ulong instanceId) => new(EntityId, instanceId);

    /// <summary>
    /// The instances whose pivot is within <paramref name="radius"/> metres of <paramref name="center"/>, nearest
    /// first. Promoted instances are not among them: their proxies are entities.
    /// </summary>
    public FoliageInstance[] FindInstances(Vector3 center, float radius)
    {
        if (!float.IsFinite(radius) || radius < 0.0f)
            return [];
        var ids = ScriptBridge.FoliageFindInRadius(EntityId, ScriptBridge.NativeVector3.FromManaged(center), radius);
        var instances = new FoliageInstance[ids.Length];
        for (var index = 0; index < ids.Length; index++)
            instances[index] = new FoliageInstance(EntityId, ids[index]);
        return instances;
    }
}

/// <summary>What a foliage instance is and where it stands.</summary>
public readonly record struct FoliageInstanceInfo(
    int TypeIndex,
    string TypeName,
    Vector3 Position,
    Quaternion Rotation,
    Vector3 Scale,
    bool HasProxyPrefab);

/// <summary>
/// One placed instance of a <see cref="FoliageComponent"/>, by the ID that stays the same while other instances come
/// and go (so it can also be saved). An instance whose type has a proxy prefab can be promoted: it leaves the foliage
/// and an entity spawned from the prefab takes its place, scripts and all. Releasing that proxy puts it back.
/// </summary>
/// <example>
/// <code>
/// if (Physics.Raycast(eye, forward, 2.0f, out var hit) &amp;&amp; hit.FoliageInstance.TryPromote(out var tree))
///     tree.GetComponent&lt;Choppable&gt;()?.Strike(power);
/// </code>
/// </example>
public readonly struct FoliageInstance : IEquatable<FoliageInstance>
{
    internal FoliageInstance(uint foliageEntityId, ulong id)
    {
        FoliageEntityId = id != 0 ? foliageEntityId : 0;
        Id = id;
    }

    /// <summary>The entity holding the <see cref="FoliageComponent"/>.</summary>
    public uint FoliageEntityId { get; }

    /// <summary>The instance's ID within its foliage; 0 when this refers to no instance.</summary>
    public ulong Id { get; }

    /// <summary>True when this refers to an instance at all. See <see cref="Exists"/> for whether it is still placed.</summary>
    public bool IsValid => FoliageEntityId != 0 && Id != 0;

    /// <summary>The entity holding the <see cref="FoliageComponent"/>.</summary>
    public GameObject Foliage => new(FoliageEntityId);

    /// <summary>True while the instance is placed: false after it was promoted or removed.</summary>
    public bool Exists => TryGetInfo(out _);

    /// <summary>The instance's type and world transform; false when it is not placed.</summary>
    public bool TryGetInfo(out FoliageInstanceInfo info)
    {
        info = default;
        if (!IsValid)
            return false;
        var request = new ScriptBridge.NativeFoliageRequest { InstanceId = Id };
        if (!ScriptBridge.ControlFoliage(FoliageEntityId, ScriptBridge.FoliageOperation.GetInstance, ref request) || request.Result == 0)
            return false;
        info = new FoliageInstanceInfo(request.TypeIndex, ScriptBridge.FoliageRequestName(ref request), request.Position.ToManaged(),
                                       request.Rotation.ToManaged(), request.Scale.ToManaged(), request.HasProxyPrefab != 0);
        return true;
    }

    /// <summary>
    /// Swaps the instance for an entity spawned from its type's proxy prefab, at the same position, rotation and scale.
    /// The proxy's scripts have run <c>OnCreate</c> by the time this returns, and its <see cref="FoliageProxyComponent"/>
    /// leads back here. False, changing nothing, when the instance is not placed or its type has no proxy prefab.
    /// </summary>
    public bool TryPromote(out GameObject proxy) => TryPromote(out proxy, out _);

    /// <inheritdoc cref="TryPromote(out GameObject)"/>
    /// <param name="proxy">The spawned entity.</param>
    /// <param name="error">Why the instance could not be promoted.</param>
    public bool TryPromote(out GameObject proxy, out string error)
    {
        proxy = new GameObject(0);
        error = string.Empty;
        if (!IsValid)
        {
            error = "Not a foliage instance.";
            return false;
        }
        var request = new ScriptBridge.NativeFoliageRequest { InstanceId = Id };
        if (!ScriptBridge.ControlFoliage(FoliageEntityId, ScriptBridge.FoliageOperation.Promote, ref request))
        {
            error = "The entity has no FoliageComponent.";
            return false;
        }
        if (request.Result == 0)
        {
            error = ScriptBridge.FoliageRequestName(ref request);
            return false;
        }
        proxy = new GameObject(request.LinkedEntity);
        return true;
    }

    public bool Equals(FoliageInstance other) => FoliageEntityId == other.FoliageEntityId && Id == other.Id;
    public override bool Equals(object? obj) => obj is FoliageInstance other && Equals(other);
    public override int GetHashCode() => HashCode.Combine(FoliageEntityId, Id);
    public static bool operator ==(FoliageInstance left, FoliageInstance right) => left.Equals(right);
    public static bool operator !=(FoliageInstance left, FoliageInstance right) => !left.Equals(right);
    public override string ToString() => IsValid ? $"Foliage {FoliageEntityId}#{Id}" : "No foliage instance";
}

/// <summary>
/// On an entity standing in for a promoted foliage instance. <see cref="Release"/> returns it to the foliage when
/// gameplay is done with it (a felled tree regrew; nobody touched a bush for a while).
/// </summary>
[NativeComponent("FoliageProxyComponent")]
public sealed class FoliageProxyComponent : RegisteredComponent
{
    internal FoliageProxyComponent(uint entityId) : base(entityId) {}

    /// <summary>The instance this entity stands for. It does not exist in the foliage until released.</summary>
    public FoliageInstance Instance
    {
        get
        {
            var request = default(ScriptBridge.NativeFoliageRequest);
            return ScriptBridge.ControlFoliage(EntityId, ScriptBridge.FoliageOperation.GetProxyLink, ref request) && request.Result != 0
                ? new FoliageInstance(request.LinkedEntity, request.InstanceId)
                : default;
        }
    }

    /// <summary>
    /// Puts the instance back into its foliage exactly as it was and destroys this entity (at the end of the frame).
    /// Returns whether the instance came back; the entity is destroyed either way.
    /// </summary>
    public bool Release()
    {
        var request = default(ScriptBridge.NativeFoliageRequest);
        return ScriptBridge.ControlFoliage(EntityId, ScriptBridge.FoliageOperation.Release, ref request) && request.Result != 0;
    }
}
