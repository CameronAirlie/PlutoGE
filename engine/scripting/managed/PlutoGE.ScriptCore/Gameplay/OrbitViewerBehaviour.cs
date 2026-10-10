using System.Numerics;

namespace PlutoGE.ScriptCore.Gameplay;

/// <summary>Reusable application camera: right-drag orbit, WASD pan, wheel zoom.</summary>
public sealed class OrbitViewerBehaviour : ScriptBehaviour
{
    [SerializedField] public GameObject? Target;
    [SerializedField] public float Distance = 7;
    [SerializedField] public float Sensitivity = 0.2f;
    [SerializedField] public float PanSpeed = 3;
    private float _yaw, _pitch = 20;
    private Vector3 _pan;
    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0 || GamePause.IsPaused) return;
        if (Input.IsMouseButtonDown(MouseButton.Right))
        {
            _yaw += Input.MouseDelta.X * Sensitivity;
            _pitch = Math.Clamp(_pitch + Input.MouseDelta.Y * Sensitivity, -85, 85);
        }
        Distance = Math.Clamp(Distance - Input.ScrollDelta.Y, 0.5f, 1000);
        var yaw = _yaw * MathF.PI / 180;
        var pitch = _pitch * MathF.PI / 180;
        var forward = new Vector3(MathF.Sin(yaw), 0, -MathF.Cos(yaw));
        var right = new Vector3(MathF.Cos(yaw), 0, MathF.Sin(yaw));
        if (Input.IsKeyDown(KeyCode.W)) _pan += forward * PanSpeed * deltaTime;
        if (Input.IsKeyDown(KeyCode.S)) _pan -= forward * PanSpeed * deltaTime;
        if (Input.IsKeyDown(KeyCode.D)) _pan += right * PanSpeed * deltaTime;
        if (Input.IsKeyDown(KeyCode.A)) _pan -= right * PanSpeed * deltaTime;
        var direction = new Vector3(MathF.Sin(yaw) * MathF.Cos(pitch), -MathF.Sin(pitch), -MathF.Cos(yaw) * MathF.Cos(pitch));
        GameObject.WorldPosition = (Target?.WorldPosition ?? Vector3.Zero) + _pan - direction * Distance;
        GameObject.WorldRotation = new Vector3(_pitch, -_yaw, 0);
    }
}
