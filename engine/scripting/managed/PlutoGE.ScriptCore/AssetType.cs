namespace PlutoGE.ScriptCore;

/// <summary>
/// Project asset types a serialized field can reference. Each name is the engine's persisted asset type
/// identifier (<c>assets::ProjectAssetTypeId</c>), so members may be added but never renamed.
/// </summary>
public enum AssetType
{
    Scene,
    Prefab,
    Mesh,
    Animation,
    AnimationClip,
    Model,
    Material,
    ShaderGraph,
    AnimationGraph,
    ParticleSystem,
    PostProcessPreset,
    Audio,
    Texture,
    ScriptableObject,
    RmlDocument,
    InputMapping,
    SurfaceResponse,
    LoadingScreen,
}
