namespace PlutoGE.ScriptCore;

/// <summary>
/// What a character wears: a list of wearable prefabs (each with a Wearable component on its root, rigged to the
/// character's armature) that the engine keeps on the character's skinned body, posed by its skeleton, with the skin
/// under them hidden. One item shows per slot: the last one listed, so wearing a cloak over a coat hides the coat until
/// the cloak is taken off. Changes take effect on the next frame. See PlutoGE docs/SKELETAL_IK.md, "Clothing and outfits".
/// </summary>
[NativeComponent("OutfitComponent")]
public sealed class OutfitComponent : RegisteredComponent
{
    private const string CountProperty = "Wearable Count";
    private const string ItemPrefix = "Wearables.";

    internal OutfitComponent(uint entityId) : base(entityId)
    {
    }

    /// <summary>The wearable prefabs (asset references), in the order they were put on. Setting replaces the list.</summary>
    public IReadOnlyList<string> Wearables
    {
        get
        {
            var count = Math.Max(0, GetInt(CountProperty));
            var wearables = new List<string>(count);
            for (var index = 0; index < count; ++index)
                if (GetString(ItemPrefix + index) is { Length: > 0 } prefab)
                    wearables.Add(prefab);
            return wearables;
        }
        set => Write(value ?? Array.Empty<string>());
    }

    /// <summary>Whether <paramref name="prefab"/> is in the list (it may be covered by a later item in its slot).</summary>
    public bool IsWearing(string prefab) => Wearables.Contains(prefab);

    /// <summary>Puts a wearable on, over whatever shows in its slot. False when it is empty or already listed.</summary>
    public bool Wear(string prefab)
    {
        if (string.IsNullOrWhiteSpace(prefab))
            return false;
        var wearables = Wearables.ToList();
        if (wearables.Contains(prefab))
            return false;
        wearables.Add(prefab);
        Write(wearables);
        return true;
    }

    /// <inheritdoc cref="Wear(string)"/>
    public bool Wear(Prefab prefab) => Wear(prefab.AssetReference);

    /// <summary>Takes a wearable off; what it covered in its slot shows again. False when it was not listed.</summary>
    public bool TakeOff(string prefab)
    {
        var wearables = Wearables.ToList();
        if (!wearables.Remove(prefab))
            return false;
        Write(wearables);
        return true;
    }

    /// <inheritdoc cref="TakeOff(string)"/>
    public bool TakeOff(Prefab prefab) => TakeOff(prefab.AssetReference);

    public void TakeOffAll() => Write(Array.Empty<string>());

    private void Write(IReadOnlyList<string> wearables)
    {
        // The count first: the entries it adds are what the engine accepts writes to.
        SetInt(CountProperty, wearables.Count);
        for (var index = 0; index < wearables.Count; ++index)
            SetString(ItemPrefix + index, wearables[index]);
    }
}
