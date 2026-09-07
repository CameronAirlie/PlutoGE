using System.Numerics;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>Attach to an entity with Camera and Camera Rig; assign the vehicle target.</summary>
public sealed class VehicleCameraRig : ScriptBehaviour
{
    [SerializedField] private GameObject? target = null;
    [SerializedField] private GameObject? alternateTarget = null;
    public override void OnCreate()
    {
        if (!CameraRig.Configure(GameObject, target, new() { Offset = new Vector3(0, 2, 7), SmoothingSeconds = 0.2f }))
            Debug.LogWarning("VehicleCameraRig requires a Camera Rig component.");
    }
    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0) return;
        if (Input.IsKeyPressed(KeyCode.C) && alternateTarget is not null) CameraRig.BlendTo(GameObject, alternateTarget, 1);
        if (Input.IsKeyPressed(KeyCode.H)) CameraRig.Shake(GameObject, 0.15f, 0.4f);
    }
}
