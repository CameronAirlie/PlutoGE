using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

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

    public float Fov
    {
        get => ScriptBridge.GetCameraFov(EntityId);
        set => ScriptBridge.SetCameraFov(EntityId, value);
    }
}
