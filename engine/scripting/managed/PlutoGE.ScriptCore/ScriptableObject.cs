namespace PlutoGE.ScriptCore;

/// <summary>
/// Base class for reusable, editor-authored data assets. Scriptable objects are
/// not components and do not need to be attached to a GameObject.
/// </summary>
public abstract class ScriptableObject
{
    public string AssetReference { get; internal set; } = string.Empty;

    internal string SerializedData { get; set; } = string.Empty;
    internal long NextRefreshTimestamp { get; set; }

    public bool IsValid => !string.IsNullOrWhiteSpace(AssetReference);
}
