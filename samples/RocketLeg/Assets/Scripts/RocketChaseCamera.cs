using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// Rocket-style third-person camera. C toggles ball focus and Tab changes the
/// followed car, which is useful for this local two-player sample.
/// </summary>
public sealed class RocketChaseCamera : ScriptBehaviour
{
    [SerializedField] private GameObject? blueCar;
    [SerializedField] private GameObject? orangeCar;
    [SerializedField] private GameObject? ball;
    [SerializedField] private float distance = 8.5f;
    [SerializedField] private float height = 3.3f;
    [SerializedField] private float lookHeight = 1.0f;
    [SerializedField] private float lookAhead = 4.0f;
    [SerializedField] private float positionSharpness = 9.0f;
    [SerializedField] private float rotationSharpness = 12.0f;
    [SerializedField] private bool startInBallCam = true;
    [SerializedField, InputMappingAsset] private string inputMappingAsset = "project://Input/RocketLeg.plutoinput";

    private GameObject? _target;
    private Vector3 _smoothedPosition;
    private Vector3 _smoothedRotation;
    private bool _ballCam;
    private InputActionMap? _inputActions;

    public override void OnCreate()
    {
        if (!string.IsNullOrWhiteSpace(inputMappingAsset))
        {
            try { _inputActions = InputActionMap.Load(inputMappingAsset); }
            catch (Exception exception)
            {
                Debug.LogError($"Unable to load RocketLeg input map '{inputMappingAsset}': {exception.Message}");
            }
        }

        blueCar ??= GameObject.Find("Blue Car");
        orangeCar ??= GameObject.Find("Orange Car");
        ball ??= GameObject.Find("Ball");
        _target = blueCar;
        _ballCam = startInBallCam;
        SnapToTarget();
    }

    public override void OnUpdate(float deltaTime)
    {
        if (_inputActions?.WasPressed("CameraBallToggle") == true) _ballCam = !_ballCam;
        if (_inputActions?.WasPressed("CameraSwitchCar") == true)
        {
            _target = _target?.EntityId == blueCar?.EntityId ? orangeCar : blueCar;
            SnapToTarget();
        }
    }

    public override void OnLateUpdate(float deltaTime)
    {
        if (_target is null || deltaTime <= 0.0f) return;

        var forward = FlatDirection(_target.Forward, -Vector3.UnitZ);
        var targetPosition = _target.WorldPosition;
        var carFocus = targetPosition + Vector3.UnitY * lookHeight;
        var cameraHeading = forward;
        var focus = carFocus + forward * lookAhead;
        var cameraDistance = distance;

        if (_ballCam && ball is not null)
        {
            var ballFocus = ball.WorldPosition + Vector3.UnitY * 0.25f;
            var flatToBall = ballFocus - carFocus;
            flatToBall.Y = 0.0f;
            if (flatToBall.LengthSquared() > 0.09f)
            {
                // Orbit opposite the ball. The car is then geometrically between
                // camera and ball instead of being allowed to drift behind view.
                cameraHeading = Vector3.Normalize(flatToBall);
            }

            // Aim between the subjects. Horizontal alignment comes from the
            // orbit above; this blend keeps both a grounded car and an airborne
            // ball inside the vertical field of view.
            focus = Vector3.Lerp(carFocus, ballFocus, 0.62f);
            var verticalSeparation = MathF.Abs(ballFocus.Y - carFocus.Y);
            cameraDistance += MathF.Max(0.0f, verticalSeparation - 3.0f) * 0.55f;
        }

        var desiredPosition = targetPosition - cameraHeading * cameraDistance + Vector3.UnitY * height;

        // Pull the camera in when the wall behind the car would obscure it.
        var cameraRay = desiredPosition - (targetPosition + Vector3.UnitY * lookHeight);
        var rayLength = cameraRay.Length();
        if (rayLength > 0.001f && Physics.Raycast(
                targetPosition + Vector3.UnitY * lookHeight,
                cameraRay / rayLength,
                rayLength,
                _target,
                out var hit))
        {
            desiredPosition = targetPosition + Vector3.UnitY * lookHeight +
                              cameraRay / rayLength * MathF.Max(1.0f, hit.Distance - 0.25f);
        }

        var desiredRotation = LookRotation(desiredPosition, focus);
        // Ball cam must follow its orbit promptly when the ball crosses behind
        // the car; a slow positional blend can pass through the car and lose it.
        var positionAmount = Damp(_ballCam ? positionSharpness * 1.8f : positionSharpness, deltaTime);
        var rotationAmount = Damp(rotationSharpness, deltaTime);
        _smoothedPosition = Vector3.Lerp(_smoothedPosition, desiredPosition, positionAmount);
        _smoothedRotation = new Vector3(
            LerpAngle(_smoothedRotation.X, desiredRotation.X, rotationAmount),
            LerpAngle(_smoothedRotation.Y, desiredRotation.Y, rotationAmount),
            LerpAngle(_smoothedRotation.Z, desiredRotation.Z, rotationAmount));

        GameObject.WorldPosition = _smoothedPosition;
        GameObject.WorldRotation = _smoothedRotation;
    }

    private void SnapToTarget()
    {
        if (_target is null) return;
        var forward = FlatDirection(_target.Forward, -Vector3.UnitZ);
        var targetPosition = _target.WorldPosition;
        var carFocus = targetPosition + Vector3.UnitY * lookHeight;
        var cameraHeading = forward;
        var focus = carFocus + forward * lookAhead;
        if (_ballCam && ball is not null)
        {
            var ballFocus = ball.WorldPosition + Vector3.UnitY * 0.25f;
            var flatToBall = ballFocus - carFocus;
            flatToBall.Y = 0.0f;
            if (flatToBall.LengthSquared() > 0.09f) cameraHeading = Vector3.Normalize(flatToBall);
            focus = Vector3.Lerp(carFocus, ballFocus, 0.62f);
        }
        _smoothedPosition = targetPosition - cameraHeading * distance + Vector3.UnitY * height;
        _smoothedRotation = LookRotation(_smoothedPosition, focus);
        GameObject.WorldPosition = _smoothedPosition;
        GameObject.WorldRotation = _smoothedRotation;
    }

    private static Vector3 LookRotation(Vector3 position, Vector3 focus)
    {
        var direction = focus - position;
        if (direction.LengthSquared() <= 0.000001f) return Vector3.Zero;
        direction = Vector3.Normalize(direction);
        var yaw = MathF.Atan2(-direction.X, -direction.Z);
        var horizontal = MathF.Sqrt(direction.X * direction.X + direction.Z * direction.Z);
        var pitch = MathF.Atan2(direction.Y, horizontal);
        var sinYaw = MathF.Sin(yaw);
        var cosYaw = MathF.Cos(yaw);
        var sinPitch = MathF.Sin(pitch);
        var cosPitch = MathF.Cos(pitch);
        const float toDegrees = 180.0f / MathF.PI;
        return new Vector3(
            MathF.Atan2(sinPitch, cosYaw * cosPitch) * toDegrees,
            MathF.Asin(Math.Clamp(sinYaw * cosPitch, -1.0f, 1.0f)) * toDegrees,
            MathF.Atan2(-sinYaw * sinPitch, cosYaw) * toDegrees);
    }

    private static Vector3 FlatDirection(Vector3 value, Vector3 fallback)
    {
        value.Y = 0.0f;
        return value.LengthSquared() > 0.0001f ? Vector3.Normalize(value) : fallback;
    }

    private static float Damp(float sharpness, float deltaTime) =>
        sharpness <= 0.0f ? 1.0f : 1.0f - MathF.Exp(-sharpness * deltaTime);

    private static float LerpAngle(float from, float to, float amount)
    {
        var delta = (to - from) % 360.0f;
        if (delta > 180.0f) delta -= 360.0f;
        if (delta < -180.0f) delta += 360.0f;
        return from + delta * Math.Clamp(amount, 0.0f, 1.0f);
    }
}
