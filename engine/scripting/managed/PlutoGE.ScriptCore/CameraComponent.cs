using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum CameraProjection { Perspective, Orthographic }

public sealed class CameraComponent : ComponentReference
{
    internal CameraComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Camera;

    public bool IsMainCamera
    {
        get => ScriptBridge.GetCameraMain(EntityId);
        set => ScriptBridge.SetCameraMain(EntityId, value);
    }

    public CameraProjection Projection
    {
        get => ScriptBridge.GetCameraProjection(EntityId);
        set => ScriptBridge.SetCameraProjection(EntityId, value);
    }

    /// <summary>Full vertical span in world units. Width follows the viewport aspect ratio.</summary>
    public float OrthographicHeight
    {
        get => ScriptBridge.GetCameraOrthographicHeight(EntityId);
        set => ScriptBridge.SetCameraOrthographicHeight(EntityId, value);
    }

    public float Fov
    {
        get => ScriptBridge.GetCameraFov(EntityId);
        set => ScriptBridge.SetCameraFov(EntityId, value);
    }
}
