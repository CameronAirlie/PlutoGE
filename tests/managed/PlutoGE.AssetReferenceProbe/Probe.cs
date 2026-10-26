using PlutoGE.ScriptCore;
namespace PlutoGE.AssetReferenceProbe;

public sealed class ProbeData : ScriptableObject
{
    [SerializedField] public int Value;
}
// Game code can define its own typed assets.
[AssetKind(AssetType.Material)] public sealed class ProbeSurface : Asset;

public sealed class AssetFieldProbe : ScriptBehaviour
{
    [SerializedField] public Prefab? Template;
    [SerializedField] public ProbeData? Data;
    [SerializedField, MaterialAsset] public string Paint = string.Empty;
    [SerializedField, InputMappingAsset] public string Controls = string.Empty;
    [SerializedField] public InputActionMap? Map;
    [SerializedField] public Prefab?[] Spawns = [];
    [SerializedField] public List<ProbeData?> Configs = [];
    [SerializedField, AssetReference(AssetType.Material)] public string[] Paints = [];
    [SerializedField] public Material? Surface;
    [SerializedField] public List<ProbeSurface?> CustomSurfaces = [];
    [SerializedField] public string Text = string.Empty;
    [SerializedField] public bool Accepted;
    [SerializedField] public int SpawnedId;

    public override void OnCreate()
    {
        var mapping = InputActionMap.Load(Controls);
        var spawned = Template?.Instantiate();
        SpawnedId = checked((int)(spawned?.EntityId ?? 0));
        Accepted = Template?.AssetReference.StartsWith("asset://", StringComparison.Ordinal) == true
            && Data is { Value: 37 } data && data.AssetReference.StartsWith("asset://", StringComparison.Ordinal)
            && Paint.StartsWith("asset://", StringComparison.Ordinal)
            && Controls.StartsWith("asset://", StringComparison.Ordinal)
            && mapping.Actions.Count == 1 && mapping.Actions[0].Name == "Use"
            && Map is { Actions.Count: 1 } map && map.AssetReference.StartsWith("asset://", StringComparison.Ordinal)
            && Spawns is [{ IsValid: true }, null] && Spawns[0]!.AssetReference.StartsWith("asset://", StringComparison.Ordinal)
            && Configs is [{ Value: 37 }]
            && Paints is [var paint] && paint.StartsWith("asset://", StringComparison.Ordinal)
            && Surface is { IsValid: true } surface && ((string)surface).StartsWith("asset://", StringComparison.Ordinal)
            && CustomSurfaces is [{ } custom] && custom.AssetReference == surface.AssetReference
            && Text == "project://Unused.plutomaterial" && spawned?.Name == "PrefabTarget";
    }
}
