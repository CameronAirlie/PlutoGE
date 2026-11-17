using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>How Fracture.Shatter breaks something (all distances in metres, world space).</summary>
public readonly record struct ShatterSettings
{
    public ShatterSettings() { }

    /// <summary>Pieces for the largest mesh in the subtree; smaller meshes get fewer (by volume), at least 1.</summary>
    public int Pieces { get; init; } = 8;

    /// <summary>The same seed breaks the same mesh the same way (on every machine).</summary>
    public int Seed { get; init; }

    /// <summary>World; used when <see cref="HasImpact"/>.</summary>
    public Vector3 ImpactPoint { get; init; }

    /// <summary>World; may be zero.</summary>
    public Vector3 ImpactDirection { get; init; }

    public bool HasImpact { get; init; }

    /// <summary>0..1: seeds pulled towards the impact (smaller pieces there).</summary>
    public float ImpactBias { get; init; } = 0.5f;

    /// <summary>kg/m³: a piece's mass is its volume times this (at least 0.05 kg).</summary>
    public float Density { get; init; } = 2600.0f;

    /// <summary>m/s outward from the source's centre.</summary>
    public float BurstSpeed { get; init; } = 2.5f;

    /// <summary>m/s along <see cref="ImpactDirection"/>.</summary>
    public float ImpactSpeed { get; init; } = 1.5f;

    /// <summary>rad/s, random angular velocity up to this.</summary>
    public float Spin { get; init; } = 4.0f;

    /// <summary>Pieces whose largest side is shorter than this are left out.</summary>
    public float MinPieceSize { get; init; } = 0.04f;

    /// <summary>Given to every piece ("" none).</summary>
    public string Tag { get; init; } = "";
}

/// <summary>
/// Breaks meshes into physics debris (docs/FRACTURE.md). Each piece is an ordinary root entity named
/// "&lt;source name&gt; piece N" with a <see cref="MeshComponent"/> (runtime geometry, the source's materials), a
/// <see cref="ColliderShape.ConvexHull"/> collider and a dynamic <see cref="RigidbodyComponent"/> already moving.
/// </summary>
/// <example><code>
/// var pieces = Fracture.Shatter(rock, new ShatterSettings { Pieces = 10, Seed = hits, HasImpact = true,
///     ImpactPoint = hit.Point, ImpactDirection = -hit.Normal, Tag = "Debris" });
/// rock.Destroy();
/// </code></example>
public static class Fracture
{
    /// <summary>True when the host can shatter meshes (older hosts: false, and Shatter returns no pieces).</summary>
    public static bool IsSupported => ScriptBridge.IsFractureSupported;

    /// <summary>Breaks every visible mesh under root into physics pieces (see docs/FRACTURE.md); returns the spawned pieces. Does not hide root.</summary>
    public static GameObject[] Shatter(GameObject root, in ShatterSettings settings)
    {
        if (root is null || !root.IsValid)
            return [];
        var request = new ScriptBridge.NativeShatterRequest
        {
            Root = root.EntityId,
            Pieces = settings.Pieces,
            Seed = unchecked((uint)settings.Seed),
            HasImpact = settings.HasImpact ? 1 : 0,
            ImpactPoint = ScriptBridge.NativeVector3.FromManaged(settings.ImpactPoint),
            ImpactDirection = ScriptBridge.NativeVector3.FromManaged(settings.ImpactDirection),
            ImpactBias = settings.ImpactBias,
            Density = settings.Density,
            BurstSpeed = settings.BurstSpeed,
            ImpactSpeed = settings.ImpactSpeed,
            Spin = settings.Spin,
            MinPieceSize = settings.MinPieceSize,
        };
        var ids = ScriptBridge.ShatterEntity(ref request, settings.Tag ?? string.Empty);
        var pieces = new GameObject[ids.Length];
        for (var index = 0; index < ids.Length; index++)
            pieces[index] = new GameObject(ids[index]);
        return pieces;
    }
}
