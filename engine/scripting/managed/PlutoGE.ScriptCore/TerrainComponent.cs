using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>
/// A heightfield terrain and its material layers. Query the ground under a point (height, normal, which layer it
/// is: dust, grass, rock...) for footsteps, effects or AI, and paint layers at runtime (tyre tracks, scorch marks).
/// Runtime painting changes this play session only; it is not saved.
/// </summary>
public sealed class TerrainComponent : ComponentReference
{
    internal TerrainComponent(uint entityId) : base(entityId) {}

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Terrain;

    /// <summary>The number of material layers (0 when the terrain has none).</summary>
    public int LayerCount
    {
        get
        {
            var request = default(ScriptBridge.NativeTerrainRequest);
            return ScriptBridge.ControlTerrain(EntityId, ScriptBridge.TerrainOperation.LayerCount, ref request) ? request.Result : 0;
        }
    }

    /// <summary>The layer's name as authored, or an empty string for an invalid index.</summary>
    public string GetLayerName(int layer)
    {
        var request = new ScriptBridge.NativeTerrainRequest { Layer = layer };
        return ScriptBridge.ControlTerrain(EntityId, ScriptBridge.TerrainOperation.LayerName, ref request) && request.Result != 0
            ? ScriptBridge.TerrainRequestName(ref request)
            : string.Empty;
    }

    /// <summary>The index of the layer with this name (case-insensitive), or -1.</summary>
    public int FindLayer(string name)
    {
        for (int layer = 0, count = LayerCount; layer < count; ++layer)
            if (string.Equals(GetLayerName(layer), name, StringComparison.OrdinalIgnoreCase))
                return layer;
        return -1;
    }

    /// <summary>The ground's world height and normal straight below (or above) a world position.</summary>
    public bool TryGetSurface(Vector3 worldPosition, out float height, out Vector3 normal)
    {
        var request = new ScriptBridge.NativeTerrainRequest { Position = ScriptBridge.NativeVector3.FromManaged(worldPosition) };
        var found = ScriptBridge.ControlTerrain(EntityId, ScriptBridge.TerrainOperation.SampleSurface, ref request) && request.Result != 0;
        height = found ? request.Height : 0.0f;
        normal = found ? request.Normal.ToManaged() : Vector3.UnitY;
        return found;
    }

    /// <summary>
    /// Each layer's weight (0..1, summing to 1) at a world position, indexed by layer; empty off the terrain.
    /// </summary>
    public float[] GetLayerWeights(Vector3 worldPosition)
    {
        var request = new ScriptBridge.NativeTerrainRequest { Position = ScriptBridge.NativeVector3.FromManaged(worldPosition) };
        if (!ScriptBridge.ControlTerrain(EntityId, ScriptBridge.TerrainOperation.LayerWeights, ref request) || request.Result == 0)
            return Array.Empty<float>();
        var weights = new float[LayerCount];
        for (var layer = 0; layer < weights.Length; ++layer)
            weights[layer] = ScriptBridge.TerrainRequestWeight(ref request, layer);
        return weights;
    }

    /// <summary>The layer covering a world position most, or -1 off the terrain or without layers.</summary>
    public int GetDominantLayer(Vector3 worldPosition)
    {
        var request = new ScriptBridge.NativeTerrainRequest { Position = ScriptBridge.NativeVector3.FromManaged(worldPosition) };
        return ScriptBridge.ControlTerrain(EntityId, ScriptBridge.TerrainOperation.DominantLayer, ref request) ? request.Result : -1;
    }

    /// <summary>
    /// Paints one dab of a layer: <paramref name="radius"/> in world units, <paramref name="opacity"/> the weight
    /// added at the centre (0..1), <paramref name="hardness"/> the fraction of the radius at full opacity. Returns
    /// whether anything changed.
    /// </summary>
    public bool PaintLayer(Vector3 worldPosition, int layer, float radius, float opacity = 1.0f, float hardness = 0.5f)
    {
        var request = new ScriptBridge.NativeTerrainRequest
        {
            Position = ScriptBridge.NativeVector3.FromManaged(worldPosition),
            Layer = layer,
            Radius = radius,
            Opacity = opacity,
            Hardness = hardness,
        };
        return ScriptBridge.ControlTerrain(EntityId, ScriptBridge.TerrainOperation.PaintLayer, ref request) && request.Result != 0;
    }
}
