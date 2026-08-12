using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class Prefab
{
    internal Prefab(string assetReference)
    {
        AssetReference = assetReference;
    }

    public string AssetReference { get; }
    public bool IsValid => !string.IsNullOrWhiteSpace(AssetReference);

    public GameObject? Instantiate()
    {
        return Instantiate(AssetReference);
    }

    public GameObject? Instantiate(Vector3 position)
    {
        return Instantiate(AssetReference, position);
    }

    public GameObject? Instantiate(Vector3 position, Vector3 rotation)
    {
        return Instantiate(AssetReference, position, rotation);
    }

    public static GameObject? Instantiate(string prefabReference)
    {
        var entityId = ScriptBridge.InstantiatePrefab(prefabReference);
        return entityId == 0 ? null : new GameObject(entityId);
    }

    /// <summary>Parses and caches immutable prefab data before time-critical spawning.</summary>
    public static bool Preload(string prefabReference) => ScriptBridge.PreloadPrefab(prefabReference);

    /// <summary>Returns true when the cached prefab data is current and ready to clone.</summary>
    public static bool IsReady(string prefabReference) => ScriptBridge.IsPrefabReady(prefabReference);

    public static GameObject? Instantiate(string prefabReference, Vector3 position)
    {
        var instance = Instantiate(prefabReference);
        if (instance is not null)
        {
            instance.Position = position;
        }

        return instance;
    }

    public static GameObject? Instantiate(string prefabReference, Vector3 position, Vector3 rotation)
    {
        var instance = Instantiate(prefabReference, position);
        if (instance is not null)
        {
            instance.Rotation = rotation;
        }

        return instance;
    }

    public override string ToString()
    {
        return AssetReference;
    }
}
