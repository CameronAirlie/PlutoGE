using System.Diagnostics.CodeAnalysis;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Managed classes a <see cref="SerializedFieldAttribute"/> member can hold as a typed project asset. Scenes
/// store the asset reference string; this table converts it to the typed value and back, and tells the
/// inspector which asset type to offer. Any <see cref="Asset"/> subclass marked <see cref="AssetKindAttribute"/>
/// is supported automatically; classes with their own loading (such as <see cref="InputActionMap"/>) are
/// registered here with <see cref="Register{T}"/>. <see cref="Prefab"/> and <see cref="ScriptableObject"/> keep
/// their dedicated field types.
/// </summary>
internal static class SerializedAssetTypes
{
    internal sealed record Handler(AssetType AssetType, Func<string, object> Load, Func<object, string> GetReference);

    private static readonly Dictionary<Type, Handler> Handlers = [];

    static SerializedAssetTypes()
    {
        Register(AssetType.InputMapping, InputActionMap.Load, map => map.AssetReference);
    }

    private static void Register<T>(AssetType assetType, Func<string, T> load, Func<T, string> getReference) where T : class =>
        Handlers.Add(typeof(T), new Handler(assetType, reference => load(reference), value => getReference((T)value)));

    internal static bool TryGet(Type type, [NotNullWhen(true)] out Handler? handler) =>
        Handlers.TryGetValue(type, out handler) || TryCreateAssetHandler(type, out handler);

    // Not cached: Asset subclasses may live in game assemblies, whose load context must stay unloadable.
    private static bool TryCreateAssetHandler(Type type, [NotNullWhen(true)] out Handler? handler)
    {
        handler = null;
        if (type.IsAbstract || !typeof(Asset).IsAssignableFrom(type) || type.GetConstructor(Type.EmptyTypes) is null ||
            type.GetCustomAttributes(typeof(AssetKindAttribute), inherit: false) is not [AssetKindAttribute kind])
            return false;

        handler = new Handler(kind.Type, reference =>
        {
            var asset = (Asset)Activator.CreateInstance(type)!;
            asset.AssetReference = reference;
            return asset;
        }, value => ((Asset)value).AssetReference);
        return true;
    }

    /// <summary>Loads a typed asset; a missing or unreadable asset leaves the field null rather than failing the script.</summary>
    internal static object? Load(Type type, string assetReference)
    {
        if (string.IsNullOrWhiteSpace(assetReference) || !TryGet(type, out var handler))
            return null;
        try
        {
            return handler.Load(assetReference);
        }
        catch (Exception exception)
        {
            Debug.LogWarning($"Could not load {handler.AssetType} asset '{assetReference}' for a {type.Name} field: {exception.Message}");
            return null;
        }
    }

    internal static string GetReference(object? value) =>
        value is not null && TryGet(value.GetType(), out var handler) ? handler.GetReference(value) : string.Empty;
}
