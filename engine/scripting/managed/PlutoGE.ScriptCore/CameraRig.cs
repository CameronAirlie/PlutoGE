using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum CameraRigMode { Follow, Orbit }

public sealed class CameraRigSettings
{
    public CameraRigMode Mode { get; set; } = CameraRigMode.Follow;
    public Vector3 Offset { get; set; } = new(0, 2, 6);
    public Vector3 FocusOffset { get; set; } = new(0, 1, 0);
    public bool FollowHeading { get; set; } = true;
    public float Yaw { get; set; }
    public float Pitch { get; set; } = 15;
    public float Distance { get; set; } = 6;
    public float SmoothingSeconds { get; set; } = 0.15f;
    public float CollisionRadius { get; set; } = 0.25f;
    public float CollisionPadding { get; set; } = 0.05f;
}

/// <summary>Runtime controls for an authored Camera Rig component on a camera entity.</summary>
public static class CameraRig
{
    public static bool Configure(GameObject camera, GameObject? target, CameraRigSettings settings)
    {
        ArgumentNullException.ThrowIfNull(camera);
        ArgumentNullException.ThrowIfNull(settings);
        return ScriptBridge.ControlCameraRig(new() {
            Camera = camera.EntityId, Target = target?.EntityId ?? 0, Mode = (int)settings.Mode,
            FollowHeading = settings.FollowHeading ? 1 : 0,
            Offset = ScriptBridge.NativeVector3.FromManaged(settings.Offset),
            FocusOffset = ScriptBridge.NativeVector3.FromManaged(settings.FocusOffset),
            Yaw = settings.Yaw, Pitch = settings.Pitch, Distance = settings.Distance,
            Smoothing = settings.SmoothingSeconds, Radius = settings.CollisionRadius, Padding = settings.CollisionPadding
        });
    }
    public static bool Orbit(GameObject camera, float yaw, float pitch, float distance) =>
        ScriptBridge.ControlCameraRig(new() { Operation = 1, Camera = camera.EntityId, Yaw = yaw, Pitch = pitch, Distance = distance });
    public static bool BlendTo(GameObject camera, GameObject target, float seconds = 0.5f) =>
        ScriptBridge.ControlCameraRig(new() { Operation = 2, Camera = camera.EntityId, Target = target.EntityId, Seconds = seconds });
    public static bool Shake(GameObject camera, float amplitude, float seconds, float frequency = 20) =>
        ScriptBridge.ControlCameraRig(new() { Operation = 3, Camera = camera.EntityId, Amplitude = amplitude, Seconds = seconds, Frequency = frequency });
    /// <summary>Clear smoothing, blending and shake; snap to the configured pose on the next simulation tick.</summary>
    public static bool Snap(GameObject camera) =>
        ScriptBridge.ControlCameraRig(new() { Operation = 4, Camera = camera.EntityId });
}
