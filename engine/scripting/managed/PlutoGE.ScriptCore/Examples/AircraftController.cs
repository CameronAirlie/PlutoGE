using System;
using System.Numerics;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Arcade aircraft controller. The aircraft's local forward axis is expected to be -Z.
/// W/S control throttle, arrows pitch/roll, and A/D or Q/E control yaw.
/// </summary>
public sealed class AircraftController : ScriptBehaviour
{
    [SerializedField] private float minimumSpeed = 12.0f;
    [SerializedField] private float cruiseSpeed = 35.0f;
    [SerializedField] private float maximumSpeed = 70.0f;
    [SerializedField] private float acceleration = 18.0f;
    [SerializedField] private float deceleration = 12.0f;

    [SerializedField] private float pitchRate = 55.0f;
    [SerializedField] private float rollRate = 85.0f;
    [SerializedField] private float yawRate = 30.0f;
    [SerializedField] private float steeringResponse = 6.0f;
    [SerializedField] private float gravity = 9.81f;
    [SerializedField] private float terminalFallSpeed = 80.0f;
    [SerializedField] private float liftAtCruise = 1.05f;
    [SerializedField] private float maximumLiftMultiplier = 2.5f;

    private RigidbodyComponent? _rigidbody;
    private float _targetSpeed;
    private Vector3 _steering;
    private Quaternion _orientation;
    private float _verticalVelocity;

    public override void OnCreate()
    {
        _rigidbody = GameObject.GetComponent<RigidbodyComponent>();
        if (_rigidbody is null)
        {
            Debug.Log("AircraftController requires a Rigidbody component.");
            return;
        }

        // One system must own the transform. A dynamic body would fight the
        // script's orientation every physics tick and visibly judder.
        _rigidbody.IsKinematic = true;
        // Bullet does not integrate gravity for kinematic bodies, so OnUpdate
        // applies the same acceleration while the sweep owns collision motion.
        _rigidbody.UseGravity = true;
        _rigidbody.FreezeRotation = true;
        _targetSpeed = Math.Clamp(cruiseSpeed, minimumSpeed, maximumSpeed);
        _orientation = GameObject.RotationQuaternion;
    }

    public override void OnUpdate(float deltaTime)
    {
        if (_rigidbody is null || deltaTime <= 0.0f)
        {
            return;
        }

        var throttle = 0.0f;
        if (Input.IsKeyDown(KeyCode.W)) throttle += 1.0f;
        if (Input.IsKeyDown(KeyCode.S)) throttle -= 1.0f;

        var speedChange = throttle >= 0.0f ? acceleration : deceleration;
        _targetSpeed = Math.Clamp(
            _targetSpeed + throttle * speedChange * deltaTime,
            minimumSpeed,
            maximumSpeed);

        var desiredSteering = Vector3.Zero;
        if (Input.IsKeyDown(KeyCode.Up)) desiredSteering.X -= 1.0f;
        if (Input.IsKeyDown(KeyCode.Down)) desiredSteering.X += 1.0f;
        if (Input.IsKeyDown(KeyCode.Left)) desiredSteering.Z += 1.0f;
        if (Input.IsKeyDown(KeyCode.Right)) desiredSteering.Z -= 1.0f;
        if (Input.IsKeyDown(KeyCode.A) || Input.IsKeyDown(KeyCode.Q)) desiredSteering.Y += 1.0f;
        if (Input.IsKeyDown(KeyCode.D) || Input.IsKeyDown(KeyCode.E)) desiredSteering.Y -= 1.0f;

        var steeringBlend = 1.0f - MathF.Exp(-MathF.Max(steeringResponse, 0.0f) * deltaTime);
        _steering = Vector3.Lerp(_steering, desiredSteering, steeringBlend);

        const float toRadians = MathF.PI / 180.0f;
        var pitch = Quaternion.CreateFromAxisAngle(Vector3.UnitX, _steering.X * pitchRate * deltaTime * toRadians);
        var yaw = Quaternion.CreateFromAxisAngle(Vector3.UnitY, _steering.Y * yawRate * deltaTime * toRadians);
        var roll = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, _steering.Z * rollRate * deltaTime * toRadians);
        var localDelta = Quaternion.Normalize(roll * yaw * pitch);
        _orientation = Quaternion.Normalize(_orientation * localDelta);
        GameObject.RotationQuaternion = _orientation;

        var forward = Vector3.Transform(-Vector3.UnitZ, _orientation);
        var aircraftUp = Vector3.Transform(Vector3.UnitY, _orientation);
        var referenceSpeed = MathF.Max(cruiseSpeed, 0.01f);
        var speedRatio = _targetSpeed / referenceSpeed;
        var liftMultiplier = Math.Clamp(
            MathF.Max(liftAtCruise, 0.0f) * speedRatio * speedRatio,
            0.0f,
            MathF.Max(maximumLiftMultiplier, 0.0f));

        // Lift acts along the wing's local up axis. Only its world-vertical
        // component offsets gravity, so banking naturally causes altitude loss.
        var verticalLift = MathF.Max(aircraftUp.Y, 0.0f) * MathF.Max(gravity, 0.0f) * liftMultiplier;
        _verticalVelocity = MathF.Max(
            _verticalVelocity + (verticalLift - MathF.Max(gravity, 0.0f)) * deltaTime,
            -MathF.Max(terminalFallSpeed, 0.0f));
        var displacement = (forward * _targetSpeed + Vector3.UnitY * _verticalVelocity) * deltaTime;
        var applied = Physics.MoveKinematic(GameObject, displacement, 0.02f);
        if (displacement.Y < 0.0f && MathF.Abs(applied.Y) < MathF.Abs(displacement.Y) * 0.5f)
            _verticalVelocity = 0.0f;
    }

}
