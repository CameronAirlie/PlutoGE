using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum ColliderShape
{
    Box = 0,
    Sphere = 1,
    Capsule = 2,
}

public sealed class ColliderComponent : ComponentReference
{
    internal ColliderComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Collider;

    public ColliderShape Shape
    {
        get => (ColliderShape)ScriptBridge.GetColliderShape(EntityId);
        set => ScriptBridge.SetColliderShape(EntityId, (int)value);
    }

    public Vector3 Center
    {
        get => ScriptBridge.GetColliderCenter(EntityId);
        set => ScriptBridge.SetColliderCenter(EntityId, value);
    }

    public Vector3 Size
    {
        get => ScriptBridge.GetColliderSize(EntityId);
        set => ScriptBridge.SetColliderSize(EntityId, value);
    }

    public float Radius
    {
        get => ScriptBridge.GetColliderRadius(EntityId);
        set => ScriptBridge.SetColliderRadius(EntityId, value);
    }

    public float Height
    {
        get => ScriptBridge.GetColliderHeight(EntityId);
        set => ScriptBridge.SetColliderHeight(EntityId, value);
    }

    public bool IsTrigger
    {
        get => ScriptBridge.GetColliderTrigger(EntityId);
        set => ScriptBridge.SetColliderTrigger(EntityId, value);
    }

    public bool BlocksAudio
    {
        get => ScriptBridge.GetColliderBlocksAudio(EntityId);
        set => ScriptBridge.SetColliderBlocksAudio(EntityId, value);
    }

    /// <summary>
    /// Writes the eight world-space corners of this collider's local box and returns true.
    /// Spheres and capsules use their bounding box (radius on X/Z, height on Y). The entity's
    /// full world transform, including scale and parent rotation, is applied.
    /// </summary>
    public bool TryGetWorldCorners(Span<Vector3> corners)
    {
        if (corners.Length < 8 || !ScriptBridge.TryGetEntityWorldMatrix(EntityId, out var world)) return false;
        var half = Shape switch
        {
            ColliderShape.Sphere => new Vector3(Radius),
            ColliderShape.Capsule => new Vector3(Radius, MathF.Max(Height * .5f, Radius), Radius),
            _ => Size * .5f,
        };
        var centre = Center;
        var index = 0;
        for (var x = -1; x <= 1; x += 2)
            for (var y = -1; y <= 1; y += 2)
                for (var z = -1; z <= 1; z += 2)
                    corners[index++] = Vector3.Transform(centre + half * new Vector3(x, y, z), world);
        return true;
    }

    /// <summary>World-space axis-aligned bounds of <see cref="TryGetWorldCorners"/>.</summary>
    public bool TryGetWorldBounds(out Vector3 minimum, out Vector3 maximum)
    {
        Span<Vector3> corners = stackalloc Vector3[8];
        minimum = maximum = default;
        if (!TryGetWorldCorners(corners)) return false;
        minimum = maximum = corners[0];
        foreach (var corner in corners)
        {
            minimum = Vector3.Min(minimum, corner);
            maximum = Vector3.Max(maximum, corner);
        }
        return true;
    }
}
