using PlutoGE.ScriptCore.Native;
using System.Numerics;

namespace PlutoGE.ScriptCore;

public enum CameraProjection { Perspective, Orthographic }

public sealed class CameraComponent : ComponentReference
{
    internal CameraComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Camera;

    /// <summary>Projects normalized viewport coordinates (top-left 0,0; bottom-right 1,1) using the current game viewport.</summary>
    public bool TryViewportToWorldRay(Vector2 point, out Vector3 origin, out Vector3 direction) =>
        ScriptBridge.TryViewportToWorldRay(EntityId, point, out origin, out direction);

    /// <summary>Returns false outside the game viewport, when unfocused, or when UI captures the pointer.</summary>
    public bool TryGetPointerRay(out Vector3 origin, out Vector3 direction)
    {
        origin = direction = default;
        return ScriptBridge.TryGetViewportPointer(out var point) && TryViewportToWorldRay(point, out origin, out direction);
    }

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
