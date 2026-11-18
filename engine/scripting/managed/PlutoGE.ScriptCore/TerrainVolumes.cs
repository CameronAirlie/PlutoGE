using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>What a terrain volume edit does inside its shape.</summary>
public enum TerrainVolumeEditMode
{
    /// <summary>The shape becomes air: digging, tunnels.</summary>
    Dig = 0,
    /// <summary>The shape becomes rock of the edit's terrain layer: filling, building up.</summary>
    Fill = 1,
    /// <summary>Rock inside the shape takes the edit's terrain layer; the ground does not move.</summary>
    Paint = 2,
}

/// <summary>A solid in world space for a terrain volume edit (metres).</summary>
public readonly record struct TerrainVolumeShape
{
    internal int Kind { get; init; }
    public Vector3 Centre { get; init; }
    /// <summary>The far end of a capsule.</summary>
    public Vector3 End { get; init; }
    public float Radius { get; init; }
    public Vector3 HalfExtents { get; init; }
    public Quaternion Rotation { get; init; }

    public static TerrainVolumeShape Sphere(Vector3 centre, float radius) =>
        new() { Kind = 0, Centre = centre, Radius = radius, Rotation = Quaternion.Identity };

    /// <summary>Every point within <paramref name="radius"/> of the segment: a tunnel, a pickaxe swing.</summary>
    public static TerrainVolumeShape Capsule(Vector3 start, Vector3 end, float radius) =>
        new() { Kind = 1, Centre = start, End = end, Radius = radius, Rotation = Quaternion.Identity };

    public static TerrainVolumeShape Box(Vector3 centre, Vector3 halfExtents, Quaternion rotation) =>
        new() { Kind = 2, Centre = centre, HalfExtents = halfExtents, Rotation = rotation };

    public static TerrainVolumeShape Box(Vector3 centre, Vector3 halfExtents) => Box(centre, halfExtents, Quaternion.Identity);

    internal void Write(ref ScriptBridge.NativeTerrainVolumeRequest request)
    {
        request.Shape = Kind;
        request.A = ScriptBridge.NativeVector3.FromManaged(Centre);
        request.B = ScriptBridge.NativeVector3.FromManaged(End);
        request.Radius = Radius;
        request.HalfExtents = ScriptBridge.NativeVector3.FromManaged(HalfExtents);
        request.Rotation = ScriptBridge.NativeQuaternion.FromManaged(Rotation == default ? Quaternion.Identity : Rotation);
    }
}

/// <summary>What a terrain volume edit did.</summary>
public readonly record struct TerrainVolumeEditResult
{
    /// <summary>The ground changed somewhere.</summary>
    public bool Changed { get; init; }
    /// <summary>The shape was larger than the budget (<see cref="TerrainVolumes.DefaultMaxExtent"/>) and nothing was done.</summary>
    public bool Refused { get; init; }
    /// <summary>Roughly how much rock was taken away, in cubic metres: turn it into resources or debris.</summary>
    public float RockRemoved { get; init; }
    /// <summary>Roughly how much rock was put back, in cubic metres.</summary>
    public float RockAdded { get; init; }
    /// <summary>The terrain layer most of the removed rock was (its index), or -1 when none was removed.</summary>
    public int MainLayer { get; init; }
    public int TerrainsChanged { get; init; }
    /// <summary>Foliage instances (trees, rocks) whose root the dig carved out, and which were removed.</summary>
    public int FoliageRemoved { get; init; }

    internal static TerrainVolumeEditResult From(in ScriptBridge.NativeTerrainVolumeRequest request) => new()
    {
        Changed = request.Result > 0,
        Refused = request.Result < 0,
        RockRemoved = request.RockRemoved,
        RockAdded = request.RockAdded,
        MainLayer = request.MainMaterial,
        TerrainsChanged = request.VolumesChanged,
        FoliageRemoved = request.FoliageRemoved,
    };
}

/// <summary>
/// Digs, fills and paints the 3D ground of terrains that have a <see cref="TerrainVolumeComponent"/>
/// (docs/TERRAIN_VOLUMES.md). Shapes are in world space and reach every terrain under them, so digging across a
/// terrain world's tiles needs no knowledge of tiles. The ground changes over the next few frames (meshing runs on
/// worker threads); <see cref="Flush"/> lands everything now.
/// </summary>
/// <example><code>
/// // A shovel: dig where the ray hit and give the player what came out.
/// var dug = TerrainVolumes.Dig(hit.Point, 0.8f);
/// if (dug.RockRemoved > 0) inventory.Add(dug.MainLayer == stoneLayer ? "Stone" : "Dirt", (int)MathF.Ceiling(dug.RockRemoved * 4));
///
/// // Save games: the edits of every terrain as bytes.
/// File.WriteAllBytes(path, TerrainVolumes.CaptureEdits());
/// TerrainVolumes.RestoreEdits(File.ReadAllBytes(path));
/// </code></example>
public static class TerrainVolumes
{
    /// <summary>Shapes wider than this (metres, on any axis) are refused unless an edit passes its own budget.</summary>
    public const float DefaultMaxExtent = 32.0f;

    /// <summary>True when the host has terrain volumes (older hosts: false, and edits do nothing).</summary>
    public static bool IsSupported => ScriptBridge.IsTerrainVolumeSupported;

    public static TerrainVolumeEditResult Dig(Vector3 centre, float radius) =>
        Edit(TerrainVolumeShape.Sphere(centre, radius), TerrainVolumeEditMode.Dig);

    /// <summary>Fills a sphere with rock of a terrain layer (its index).</summary>
    public static TerrainVolumeEditResult Fill(Vector3 centre, float radius, int layer = 0) =>
        Edit(TerrainVolumeShape.Sphere(centre, radius), TerrainVolumeEditMode.Fill, layer);

    public static TerrainVolumeEditResult Tunnel(Vector3 start, Vector3 end, float radius) =>
        Edit(TerrainVolumeShape.Capsule(start, end, radius), TerrainVolumeEditMode.Dig);

    public static TerrainVolumeEditResult Paint(Vector3 centre, float radius, int layer) =>
        Edit(TerrainVolumeShape.Sphere(centre, radius), TerrainVolumeEditMode.Paint, layer);

    /// <summary>
    /// Any shape and mode. <paramref name="maxExtent"/> overrides the budget (0 accepts any size); null uses
    /// <see cref="DefaultMaxExtent"/>. Dig edits remove foliage whose root they carve out.
    /// </summary>
    public static TerrainVolumeEditResult Edit(TerrainVolumeShape shape, TerrainVolumeEditMode mode, int layer = 0, float? maxExtent = null)
    {
        var request = new ScriptBridge.NativeTerrainVolumeRequest
        {
            Mode = (int)mode,
            Material = layer,
            MaxExtent = maxExtent ?? -1.0f,
        };
        shape.Write(ref request);
        return ScriptBridge.ControlTerrainVolume(0, ScriptBridge.TerrainVolumeOperation.Edit, ref request)
            ? TerrainVolumeEditResult.From(request)
            : default;
    }

    /// <summary>Lands every terrain's pending meshes and holes now (after loading a save, before a cutscene).</summary>
    public static void Flush()
    {
        var request = new ScriptBridge.NativeTerrainVolumeRequest();
        ScriptBridge.ControlTerrainVolume(0, ScriptBridge.TerrainVolumeOperation.Flush, ref request);
    }

    /// <summary>
    /// Every terrain's edits as bytes, for a save game: terrain worlds keep their tiles' edits even while the tiles are
    /// streamed out. Empty when unsupported.
    /// </summary>
    public static byte[] CaptureEdits()
    {
        var request = new ScriptBridge.NativeTerrainVolumeRequest();
        if (!ScriptBridge.ControlTerrainVolume(0, ScriptBridge.TerrainVolumeOperation.CaptureEdits, ref request) || request.Result <= 0)
            return [];
        var bytes = new byte[request.Result];
        return ScriptBridge.ControlTerrainVolume(0, ScriptBridge.TerrainVolumeOperation.CaptureEdits, ref request, bytes) &&
               request.Result == bytes.Length
            ? bytes
            : [];
    }

    /// <summary>
    /// Gives every terrain the edits of a <see cref="CaptureEdits"/>; terrains it does not name go back to their files.
    /// False when the bytes are damaged (nothing changes).
    /// </summary>
    public static bool RestoreEdits(byte[] edits)
    {
        if (edits is null || edits.Length == 0) return false;
        var request = new ScriptBridge.NativeTerrainVolumeRequest();
        return ScriptBridge.ControlTerrainVolume(0, ScriptBridge.TerrainVolumeOperation.RestoreEdits, ref request, edits) && request.Result == 1;
    }

    /// <summary>Every terrain goes back to its files (a new game).</summary>
    public static void ClearEdits()
    {
        var request = new ScriptBridge.NativeTerrainVolumeRequest();
        ScriptBridge.ControlTerrainVolume(0, ScriptBridge.TerrainVolumeOperation.ClearEdits, ref request);
    }
}

/// <summary>
/// 3D ground on one terrain (or a terrain world's tile template): caves, pits and mounds the heightfield cannot hold.
/// Most gameplay uses <see cref="TerrainVolumes"/>, which finds the terrains under a shape; this edits one terrain only,
/// without a budget or foliage removal.
/// </summary>
[NativeComponent("TerrainVolumeComponent")]
public sealed class TerrainVolumeComponent : RegisteredComponent
{
    internal TerrainVolumeComponent(uint entityId) : base(entityId)
    {
    }

    /// <summary>Metres between density samples. Changing it starts the volume again from its file.</summary>
    public float VoxelSize { get => GetFloat("VoxelSize"); set => SetFloat("VoxelSize", value); }
    public int ChunkVoxels { get => GetInt("ChunkVoxels"); set => SetInt("ChunkVoxels", value); }
    public bool Collision { get => GetBool("Collision"); set => SetBool("Collision", value); }
    public int MeshesPerFrame { get => GetInt("MeshesPerFrame"); set => SetInt("MeshesPerFrame", value); }

    public TerrainVolumeEditResult Edit(TerrainVolumeShape shape, TerrainVolumeEditMode mode, int layer = 0)
    {
        var request = new ScriptBridge.NativeTerrainVolumeRequest { Mode = (int)mode, Material = layer };
        shape.Write(ref request);
        return ScriptBridge.ControlTerrainVolume(EntityId, ScriptBridge.TerrainVolumeOperation.Edit, ref request)
            ? TerrainVolumeEditResult.From(request)
            : default;
    }

    /// <summary>Whether every edit's meshes and holes have landed.</summary>
    public bool IsUpToDate
    {
        get
        {
            var request = new ScriptBridge.NativeTerrainVolumeRequest();
            return !ScriptBridge.ControlTerrainVolume(EntityId, ScriptBridge.TerrainVolumeOperation.IsUpToDate, ref request) || request.Result == 1;
        }
    }

    /// <summary>The chunk entities drawing and colliding with the dug ground.</summary>
    public int ChunkCount
    {
        get
        {
            var request = new ScriptBridge.NativeTerrainVolumeRequest();
            return ScriptBridge.ControlTerrainVolume(EntityId, ScriptBridge.TerrainVolumeOperation.ChunkCount, ref request) ? request.Result : 0;
        }
    }

    public void Flush()
    {
        var request = new ScriptBridge.NativeTerrainVolumeRequest();
        ScriptBridge.ControlTerrainVolume(EntityId, ScriptBridge.TerrainVolumeOperation.Flush, ref request);
    }

    /// <summary>Writes the volume file (tools; save games use <see cref="TerrainVolumes.CaptureEdits"/>).</summary>
    public bool Save()
    {
        var request = new ScriptBridge.NativeTerrainVolumeRequest();
        return ScriptBridge.ControlTerrainVolume(EntityId, ScriptBridge.TerrainVolumeOperation.Save, ref request) && request.Result == 1;
    }
}
