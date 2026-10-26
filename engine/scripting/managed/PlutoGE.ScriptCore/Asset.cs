namespace PlutoGE.ScriptCore;

/// <summary>
/// A typed reference to a project asset. Any concrete subclass marked with <see cref="AssetKindAttribute"/> works
/// as a <see cref="SerializedFieldAttribute"/> member or array element: the inspector offers matching assets and
/// the asset ships with builds. Defining a new asset type, including in game code, is a one-line class.
/// Converts implicitly to its reference string, so it can be passed wherever an asset reference is accepted.
/// </summary>
public abstract class Asset
{
    public string AssetReference { get; internal set; } = string.Empty;
    public bool IsValid => !string.IsNullOrWhiteSpace(AssetReference);

    public override string ToString() => AssetReference;

    public static implicit operator string(Asset? asset) => asset?.AssetReference ?? string.Empty;
}

/// <summary>The project asset type an <see cref="Asset"/> subclass references.</summary>
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class AssetKindAttribute(AssetType type) : Attribute
{
    public AssetType Type { get; } = type;
}

[AssetKind(AssetType.Material)] public sealed class Material : Asset;
/// <summary>A shader graph asset.</summary>
[AssetKind(AssetType.ShaderGraph)] public sealed class Shader : Asset;
[AssetKind(AssetType.Texture)] public sealed class Texture : Asset;
[AssetKind(AssetType.Mesh)] public sealed class Mesh : Asset;
[AssetKind(AssetType.Model)] public sealed class Model : Asset;
[AssetKind(AssetType.Audio)] public sealed class AudioClip : Asset;
[AssetKind(AssetType.Scene)] public sealed class SceneAsset : Asset;
[AssetKind(AssetType.Animation)] public sealed class Animation : Asset;
[AssetKind(AssetType.AnimationClip)] public sealed class AnimationClip : Asset;
[AssetKind(AssetType.AnimationGraph)] public sealed class AnimationGraph : Asset;
[AssetKind(AssetType.ParticleSystem)] public sealed class ParticleSystem : Asset;
[AssetKind(AssetType.PostProcessPreset)] public sealed class PostProcessPreset : Asset;
[AssetKind(AssetType.RmlDocument)] public sealed class RmlDocumentAsset : Asset;
[AssetKind(AssetType.SurfaceResponse)] public sealed class SurfaceResponseAsset : Asset;
[AssetKind(AssetType.LoadingScreen)] public sealed class LoadingScreen : Asset;
[AssetKind(AssetType.UITheme)] public sealed class UIThemeAsset : Asset;
