using System;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum SurfaceEvent { Footstep, Impact }

public readonly record struct SurfaceResponse(float Friction, string Sound, string Particles, string DecalMaterial)
{
    public static SurfaceResponse Default => new(0.5f, string.Empty, string.Empty, string.Empty);
}

/// <summary>Resolves gameplay effects from an entity's collider, including terrain colliders.</summary>
public static class SurfaceResponses
{
    /// <summary>Returns false for unknown surfaces and still supplies a safe default response.</summary>
    public static bool TryResolve(GameObject? entity, SurfaceEvent kind, out SurfaceResponse response)
    {
        response = SurfaceResponse.Default;
        if (entity is null || !entity.IsValid || !Enum.IsDefined(kind)) return false;
        return ScriptBridge.ResolveSurfaceResponse(entity.EntityId, kind, out response);
    }

    public static SurfaceResponse Resolve(RaycastHit hit, SurfaceEvent kind)
    {
        TryResolve(hit.Entity, kind, out var response);
        return response;
    }
}
