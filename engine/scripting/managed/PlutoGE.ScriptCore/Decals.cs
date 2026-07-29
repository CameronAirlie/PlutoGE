using System;
using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Creates projected surface decals such as bullet holes, paint, and scorch marks.</summary>
public static class Decals
{
    public static GameObject? Spawn(
        RaycastHit hit,
        string materialAssetReference,
        Vector2 size,
        float depth = 0.1f,
        float lifetime = 0.0f,
        float fadeDuration = 0.0f)
    {
        return Spawn(hit.Point, hit.Normal, materialAssetReference, size, depth, lifetime, fadeDuration);
    }

    public static GameObject? Spawn(
        Vector3 point,
        Vector3 normal,
        string materialAssetReference,
        Vector2 size,
        float depth = 0.1f,
        float lifetime = 0.0f,
        float fadeDuration = 0.0f)
    {
        if (string.IsNullOrWhiteSpace(materialAssetReference) ||
            normal.LengthSquared() <= 0.000001f ||
            !IsFinite(point) || !IsFinite(normal) ||
            !float.IsFinite(size.X) || !float.IsFinite(size.Y) ||
            size.X <= 0.0f || size.Y <= 0.0f ||
            !float.IsFinite(depth) || depth <= 0.0f ||
            !float.IsFinite(lifetime) || !float.IsFinite(fadeDuration))
        {
            return null;
        }

        var entityId = ScriptBridge.SpawnDecal(
            point,
            Vector3.Normalize(normal),
            materialAssetReference,
            size,
            depth,
            MathF.Max(0.0f, lifetime),
            Math.Clamp(fadeDuration, 0.0f, MathF.Max(0.0f, lifetime)));
        return entityId == 0 ? null : new GameObject(entityId);
    }

    private static bool IsFinite(Vector3 value)
    {
        return float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);
    }
}
