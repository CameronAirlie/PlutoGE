using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// Two-player, physics-backed arcade car controller. Each player can use their
/// matching gamepad, with the original keyboard controls retained.
/// </summary>
public sealed class ArcadeCarController : ScriptBehaviour
{
    [SerializedField] private int playerNumber  = 1;
    [SerializedField] private float acceleration  = 28.0f;
    [SerializedField] private float boostAcceleration  = 23.0f;
    // maximumSpeed is the unboosted drive limit. maximumBoostSpeed is also the
    // hard cap for total linear velocity from boost, impacts, and falling.
    [SerializedField] private float maximumSpeed  = 18.0f;
    [SerializedField] private float maximumBoostSpeed  = 25.0f;
    [SerializedField] private float maximumAngularSpeed  = 3.0f;
    // Exponential angular-momentum damping in inverse seconds. A value of 1
    // removes roughly 63% of an uncontrolled spin per second.
    [SerializedField] private float angularMomentumDrag = 1.0f;
    [SerializedField] private float lateralGrip  = 8.0f;
    [SerializedField] private float groundSteerForce  = 2600.0f;
    [SerializedField] private float airControlForce  = 3000.0f;
    [SerializedField] private float groundedYawDamping  = 520.0f;
    [SerializedField] private float controlLeverArm  = 1.35000002f;
    [SerializedField] private float jumpImpulse  = 7.0f;
    [SerializedField] private float jumpBufferDuration = 0.16f;
    [SerializedField] private float groundProbeDistance = 1.6f;
    [SerializedField] private float respawnHeight  = -4.0f;
    [SerializedField, InputMappingAsset] private string inputMappingAsset = "project://Input/RocketLeg.plutoinput";

    private RigidbodyComponent? _body;
    private Vector3 _spawnPosition;
    private Vector3 _spawnRotation;
    private float _throttle;
    private float _steering;
    private float _pitch;
    private float _roll;
    private bool _freeAirRoll;
    private bool _boosting;
    private bool _jumpQueued;
    private float _jumpBufferTimer;
    private float _groundedGraceTimer;
    private InputActionMap? _inputActions;
    private string _actionPrefix = "P1";

    public override void OnCreate()
    {
        _actionPrefix = $"P{Math.Max(playerNumber, 1)}";
        if (!string.IsNullOrWhiteSpace(inputMappingAsset))
        {
            try { _inputActions = InputActionMap.Load(inputMappingAsset); }
            catch (Exception exception)
            {
                Debug.LogError($"Unable to load RocketLeg input map '{inputMappingAsset}': {exception.Message}");
            }
        }

        _spawnPosition = GameObject.WorldPosition;
        _spawnRotation = GameObject.WorldRotation;
        _body = GameObject.GetComponent<RigidbodyComponent>();
        if (_body is null)
        {
            Debug.LogError($"{GameObject.Name} needs a RigidbodyComponent.");
            return;
        }

        _body.Mass = 850.0f;
        // Angular momentum is damped explicitly in OnFixedUpdate. Keeping the
        // native value at zero avoids applying a second, backend-dependent drag.
        _body.LinearDrag = 0.04f;
        _body.AngularDrag = 0.0f;
        _body.Friction = 0.9f;
        _body.UseGravity = true;
        _body.IsKinematic = false;
        _body.FreezeRotation = false;
    }

    public override void OnUpdate(float deltaTime)
    {
        var forwards = GetAxis("Forward");
        var backwards = GetAxis("Backward");
        _throttle = Math.Clamp(forwards - backwards, -1.0f, 1.0f);
        _steering = GetAxis("Steer");
        _pitch = GetAxis("Pitch");
        _roll = GetAxis("AirRoll");
        _freeAirRoll = IsDown("FreeAirRoll");
        _boosting = IsDown("Boost");
        if (WasPressed("Jump"))
        {
            _jumpQueued = true;
            _jumpBufferTimer = MathF.Max(jumpBufferDuration, 0.0f);
        }

        if (GameObject.WorldPosition.Y < respawnHeight)
        {
            ResetCar();
        }
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_body is null || _body.IsKinematic)
        {
            _jumpQueued = false;
            _jumpBufferTimer = 0.0f;
            return;
        }

        if (_jumpQueued)
        {
            _jumpBufferTimer = MathF.Max(0.0f, _jumpBufferTimer - fixedDeltaTime);
            if (_jumpBufferTimer <= 0.0f) _jumpQueued = false;
        }

        var velocity = _body.Velocity;
        var forward = SafeDirection(GameObject.Forward, -Vector3.UnitZ);
        var right = SafeDirection(GameObject.Right, Vector3.UnitX);
        var up = SafeDirection(Vector3.Cross(right, forward), Vector3.UnitY);
        var grounded = IsGrounded() && Vector3.Dot(up, Vector3.UnitY) > 0.25f;

        if (grounded)
        {
            _groundedGraceTimer = 0.12f;
        }
        else
        {
            _groundedGraceTimer = MathF.Max(0.0f, _groundedGraceTimer - fixedDeltaTime);
        }

        if (_jumpQueued && _groundedGraceTimer > 0.0f)
        {
            _body.AddImpulse(up * jumpImpulse * _body.Mass);
            _jumpQueued = false;
            _jumpBufferTimer = 0.0f;
            _groundedGraceTimer = 0.0f;
            grounded = false;
        }

        if (grounded)
        {
            var groundForward = Flatten(forward, -Vector3.UnitZ);
            var groundRight = Flatten(right, Vector3.UnitX);
            var forwardSpeed = Vector3.Dot(velocity, groundForward);
            var sideSpeed = Vector3.Dot(velocity, groundRight);
            var requestedDirection = MathF.Sign(_throttle);
            var opposingMotion = requestedDirection != 0.0f && MathF.Sign(forwardSpeed) != requestedDirection;
            var belowDriveLimit = MathF.Abs(forwardSpeed) < maximumSpeed || opposingMotion;
            if (MathF.Abs(_throttle) > 0.001f && belowDriveLimit)
            {
                _body.AddForce(groundForward * (_throttle * acceleration * _body.Mass));
            }

            // Tyre grip is a physical lateral force, so impacts can still push
            // the chassis sideways instead of having velocity overwritten.
            var gripForce = -groundRight * sideSpeed * lateralGrip * _body.Mass;
            _body.AddForce(ClampMagnitude(gripForce, _body.Mass * 40.0f));

            if (MathF.Abs(_steering) > 0.001f && MathF.Abs(forwardSpeed) > 0.2f)
            {
                var travelDirection = MathF.Abs(_throttle) > 0.05f
                    ? MathF.Sign(_throttle)
                    : MathF.Sign(forwardSpeed);
                var speedRatio = Math.Clamp(MathF.Abs(forwardSpeed) / MathF.Max(maximumSpeed, 1.0f), 0.0f, 1.0f);
                var steeringForce = groundSteerForce * (1.0f - speedRatio * 0.4f);
                ApplyForceCouple(-up * (_steering * travelDirection), forward, steeringForce);
            }

            // Simulate tyre resistance to chassis yaw with an opposing torque.
            // This stabilizes ground steering without touching AngularVelocity;
            // the same car remains free-spinning once it leaves the floor.
            var yawRate = Vector3.Dot(_body.AngularVelocity, up);
            if (MathF.Abs(yawRate) > 0.01f)
            {
                var dampingForce = MathF.Min(MathF.Abs(yawRate) * groundedYawDamping, groundSteerForce);
                ApplyForceCouple(-up * MathF.Sign(yawRate), forward, dampingForce);
            }
        }
        else
        {
            // These equal-and-opposite off-centre forces produce torque with
            // zero net linear force. No transform or angular velocity is set.
            // Stick Y pitches the nose. Stick X yaws normally, but while the
            // left bumper is held it produces roll instead (free air roll).
            // X adds a dedicated right-roll input in either mode.
            var aerialYaw = _freeAirRoll ? 0.0f : _steering;
            var aerialRoll = Math.Clamp(_roll + (_freeAirRoll ? _steering : 0.0f), -1.0f, 1.0f);
            ApplyForceCouple(right * _pitch, forward, airControlForce);
            ApplyForceCouple(-up * aerialYaw, forward, airControlForce * 0.72f);
            ApplyForceCouple(forward * aerialRoll, right, airControlForce);
        }

        // Boost is a real force along the complete chassis forward vector. It
        // therefore follows pitch in the air and can accelerate the car upward.
        var boostForwardSpeed = Vector3.Dot(velocity, forward);
        if (_boosting && boostForwardSpeed < maximumBoostSpeed)
        {
            _body.AddForce(forward * boostAcceleration * _body.Mass);
        }

        // Apply frame-rate-independent drag directly against angular momentum.
        // This changes rotational velocity, not orientation, so impacts and air
        // controls still produce genuine momentum that then decays over time.
        var angularDamping = MathF.Exp(-MathF.Max(angularMomentumDrag, 0.0f) * fixedDeltaTime);
        _body.AngularVelocity *= angularDamping;

        // Forces and collisions still determine both momentum vectors. Only
        // their magnitudes are capped, preserving combined pitch/yaw/roll.
        _body.Velocity = ClampMagnitude(_body.Velocity, MathF.Max(maximumBoostSpeed, 0.0f));
        _body.AngularVelocity = ClampMagnitude(_body.AngularVelocity, MathF.Max(maximumAngularSpeed, 0.0f));

    }

    public void ResetCar()
    {
        ResetCar(_spawnPosition, _spawnRotation);
    }

    public void ResetCar(Vector3 worldPosition, Vector3 worldRotation)
    {
        GameObject.WorldPosition = worldPosition;
        GameObject.WorldRotation = worldRotation;
        _jumpQueued = false;
        _jumpBufferTimer = 0.0f;
        _groundedGraceTimer = 0.0f;
        if (_body is not null)
        {
            _body.Velocity = Vector3.Zero;
            _body.AngularVelocity = Vector3.Zero;
        }
    }

    public void SetFrozen(bool frozen)
    {
        if (_body is null) return;
        _body.Velocity = Vector3.Zero;
        _body.AngularVelocity = Vector3.Zero;
        _body.IsKinematic = frozen;
    }

    private bool IsGrounded()
    {
        var center = GameObject.WorldPosition + Vector3.UnitY * 0.2f;
        if (HasGroundBelow(center)) return true;

        // A centre-only ray can miss while the chassis is rocking even though
        // one or more corners are in contact. Probe a wheel-like footprint so
        // valid landings consistently refresh jump grace.
        var scale = GameObject.Scale;
        var right = SafeDirection(GameObject.Right, Vector3.UnitX) * MathF.Max(MathF.Abs(scale.X) * 0.36f, 0.3f);
        var forward = SafeDirection(GameObject.Forward, -Vector3.UnitZ) * MathF.Max(MathF.Abs(scale.Z) * 0.36f, 0.5f);
        return HasGroundBelow(center + right + forward) ||
               HasGroundBelow(center + right - forward) ||
               HasGroundBelow(center - right + forward) ||
               HasGroundBelow(center - right - forward);
    }

    private bool HasGroundBelow(Vector3 origin)
    {
        return Physics.RaycastTagged(
                   origin,
                   -Vector3.UnitY,
                   MathF.Max(groundProbeDistance, 0.1f),
                   "ground",
                   GameObject,
                   out var hit) &&
               Vector3.Dot(hit.Normal, Vector3.UnitY) > 0.5f;
    }

    private float GetAxis(string action)
    {
        return _inputActions?.GetAxis(_actionPrefix + action) ?? 0.0f;
    }

    private bool IsDown(string action)
    {
        return _inputActions?.IsDown(_actionPrefix + action) ?? false;
    }

    private bool WasPressed(string action)
    {
        return _inputActions?.WasPressed(_actionPrefix + action) ?? false;
    }

    private static Vector3 Flatten(Vector3 value, Vector3 fallback)
    {
        value.Y = 0.0f;
        return value.LengthSquared() > 0.0001f ? Vector3.Normalize(value) : fallback;
    }

    private void ApplyForceCouple(Vector3 torqueAxis, Vector3 leverDirection, float forceMagnitude)
    {
        if (_body is null || torqueAxis.LengthSquared() < 0.0001f || leverDirection.LengthSquared() < 0.0001f)
        {
            return;
        }

        var axis = Vector3.Normalize(torqueAxis);
        var lever = Vector3.Normalize(leverDirection) * MathF.Max(controlLeverArm, 0.1f);
        var forceDirection = Vector3.Cross(axis, Vector3.Normalize(leverDirection));
        if (forceDirection.LengthSquared() < 0.0001f) return;
        var force = Vector3.Normalize(forceDirection) * MathF.Max(forceMagnitude, 0.0f);
        var center = GameObject.WorldPosition;
        _body.AddForceAtPosition(force, center + lever);
        _body.AddForceAtPosition(-force, center - lever);
    }

    private static Vector3 SafeDirection(Vector3 value, Vector3 fallback)
    {
        return value.LengthSquared() > 0.0001f ? Vector3.Normalize(value) : fallback;
    }

    private static Vector3 ClampMagnitude(Vector3 value, float maximumLength)
    {
        var lengthSquared = value.LengthSquared();
        if (lengthSquared <= maximumLength * maximumLength) return value;
        return Vector3.Normalize(value) * maximumLength;
    }
}
