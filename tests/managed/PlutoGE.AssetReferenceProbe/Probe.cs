using PlutoGE.ScriptCore;
namespace PlutoGE.AssetReferenceProbe;

public sealed class ProbeData : ScriptableObject
{
    [SerializedField] public int Value;
}
public sealed class AssetFieldProbe : ScriptBehaviour
{
    [SerializedField] public Prefab? Template;
    [SerializedField] public ProbeData? Data;
    [SerializedField, MaterialAsset] public string Paint = string.Empty;
    [SerializedField, InputMappingAsset] public string Controls = string.Empty;
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
            && Text == "project://Unused.plutomaterial" && spawned?.Name == "PrefabTarget";
    }
}
