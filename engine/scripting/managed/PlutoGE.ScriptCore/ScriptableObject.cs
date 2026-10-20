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
    // Last-write time of the loose asset file when it was last read, for change polling.
    internal DateTime? SourceWriteTime { get; set; }

    public bool IsValid => !string.IsNullOrWhiteSpace(AssetReference);

    /// <summary>
    /// Loads a data asset by reference at runtime, for example to restore an item definition
    /// recorded in a save file. Returns null when the reference is empty, missing, or names an
    /// asset of a different type. Each call materializes a new instance.
    /// </summary>
    public static T? Load<T>(string assetReference) where T : ScriptableObject =>
        string.IsNullOrWhiteSpace(assetReference) ? null : Native.ScriptBridge.LoadScriptableObject<T>(assetReference);
}
