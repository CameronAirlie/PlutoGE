using System;
using System.Collections.Generic;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Simple arcade raycast vehicle controller tuned for fast, forgiving handling.
/// </summary>
public sealed class RaycastVehicleController : ScriptBehaviour
{
    [SerializedField] private GameObject? frontLeftWheelAnchor = null;
    [SerializedField] private GameObject? frontRightWheelAnchor = null;
    [SerializedField] private GameObject? rearLeftWheelAnchor = null;
    [SerializedField] private GameObject? rearRightWheelAnchor = null;

    [SerializedField] private GameObject? frontLeftWheelVisual = null;
    [SerializedField] private GameObject? frontRightWheelVisual = null;
    [SerializedField] private GameObject? rearLeftWheelVisual = null;
    [SerializedField] private GameObject? rearRightWheelVisual = null;

    [SerializedField] private float mass   = 1250.0f;
    [SerializedField] private float wheelRadius   = 0.5f;
    [SerializedField] private float suspensionTravel   = 0.100000001f;
    [SerializedField] private float rideHeight   = 0.150000006f;
    [SerializedField] private float springStrength   = 118000.0f;
    [SerializedField] private float damperStrength   = 12000.0f;

    [SerializedField] private float acceleration   = 26.0f;
    [SerializedField] private float reverseAcceleration   = 14.0f;
    [SerializedField] private float brakePower   = 34.0f;
    [SerializedField] private float maxSpeed   = 92.0f;
    [SerializedField] private int drivetrain   = 0;
    [SerializedField] private float frontGrip   = 7.5f;
    [SerializedField] private float rearGrip   = 9.0f;
    [SerializedField] private float gripLimit   = 1.35f;
    [SerializedField] private float driveGrip   = 1.15f;
    [SerializedField] private float brakeGrip   = 1.45f;
    [SerializedField] private float stabilityAssist   = 4.0f;
    [SerializedField] private float handbrakeGrip   = 2.7f;
    [SerializedField] private float driftAssist   = 5.0f;
    [SerializedField] private float downforce   = 5.0f;
    [SerializedField] private float idleRpm = 900.0f;
    [SerializedField] private float redlineRpm = 7200.0f;
    [SerializedField] private float finalDriveRatio = 3.7f;
    [SerializedField] private float reverseGearRatio = 3.2f;
    [SerializedField] private float firstGearRatio = 3.1f;
    [SerializedField] private float secondGearRatio = 2.2f;
    [SerializedField] private float thirdGearRatio = 1.6f;
    [SerializedField] private float fourthGearRatio = 1.25f;
    [SerializedField] private float fifthGearRatio = 1.0f;
    [SerializedField] private float sixthGearRatio = 0.82f;
    [SerializedField] private float upshiftRpm = 6200.0f;
    [SerializedField] private float downshiftRpm = 1800.0f;
    [SerializedField] private float engineResponse = 9.0f;
    [SerializedField] private float launchRpm = 3400.0f;
    [SerializedField] private float peakTorqueRpm = 4800.0f;
    [SerializedField] private float shiftDuration = 0.18f;

    [SerializedField] private float maxSteerAngle   = 32.0f;
    [SerializedField] private float steerSharpness   = 5.0f;
    [SerializedField] private float highSpeedSteerFade   = 0.620000005f;
    [SerializedField] private float yawAssist   = 0.0f;
    [SerializedField] private float uprightAssist   = 0.0f;

    [SerializedField] private bool steerFrontWheelAnchors   = true;
    [SerializedField] private float physicsSteerDirection   = -1.0f;
    [SerializedField] private Vector3 wheelSpinAxis   = new Vector3(-1.0f, 0.0f, 0.0f);
    [SerializedField] private Vector3 wheelSteerAxis   = new Vector3(0.0f, 1.0f, 0.0f);
    [SerializedField] private float leftWheelSpinDirection   = 1.0f;
    [SerializedField] private float rightWheelSpinDirection   = 1.0f;

    [SerializedField] private bool lockCursorOnCreate   = true;
    [SerializedField] private string groundTag   = "ground";

    private readonly WheelState[] _wheels = new WheelState[4];
    private static readonly Dictionary<uint, VehicleTelemetry> s_telemetryByEntity = [];
    private RigidbodyComponent? _rigidbody;
    private float _steerAngle;
    private float _engineRpm;
    private float _drivenWheelSpinSpeed;
    private float _shiftTimer;
    private int _currentGear = 1;
    private int _groundedWheelCount;

    public static bool TryGetTelemetry(GameObject? vehicle, out VehicleTelemetry telemetry)
    {
        telemetry = default;
        return vehicle is not null && s_telemetryByEntity.TryGetValue(vehicle.EntityId, out telemetry);
    }

    public static VehicleTelemetry GetTelemetryOrDefault(GameObject? vehicle)
    {
        return TryGetTelemetry(vehicle, out var telemetry) ? telemetry : default;
    }

    public override void OnCreate()
    {
        _rigidbody = GameObject.GetComponent<RigidbodyComponent>();
        if (_rigidbody is not null)
        {
            _rigidbody.Mass = MathF.Max(mass, 50.0f);
            _rigidbody.IsKinematic = false;
            _rigidbody.UseGravity = true;
            _rigidbody.FreezeRotation = false;
            _rigidbody.LinearDrag = 0.015f;
            _rigidbody.AngularDrag = 0.75f;
            _rigidbody.Friction = 0.02f;
        }

        _engineRpm = MathF.Max(idleRpm, 0.0f);
        _drivenWheelSpinSpeed = 0.0f;
        _shiftTimer = 0.0f;
        _currentGear = 1;

        if (lockCursorOnCreate)
        {
            Input.CursorLocked = true;
        }

        ConfigureWheel(0, frontLeftWheelAnchor, frontLeftWheelVisual, true, false);
        ConfigureWheel(1, frontRightWheelAnchor, frontRightWheelVisual, true, true);
        ConfigureWheel(2, rearLeftWheelAnchor, rearLeftWheelVisual, false, false);
        ConfigureWheel(3, rearRightWheelAnchor, rearRightWheelVisual, false, true);
    }

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0.0f || _rigidbody is null)
        {
            return;
        }

        if (Input.IsKeyPressed(KeyCode.Escape))
        {
            Input.CursorLocked = !Input.CursorLocked;
        }

        if (Input.IsKeyPressed(KeyCode.R))
        {
            RecoverCar();
        }

        var throttle = Input.IsKeyDown(KeyCode.W) ? 1.0f : 0.0f;
        var brake = Input.IsKeyDown(KeyCode.S) ? 1.0f : 0.0f;
        var handbrake = Input.IsKeyDown(KeyCode.Space);
        var steerInput = 0.0f;
        if (Input.IsKeyDown(KeyCode.A)) steerInput += 1.0f;
        if (Input.IsKeyDown(KeyCode.D)) steerInput -= 1.0f;

        var forwardSpeed = Vector3.Dot(_rigidbody.Velocity, Forward);
        var speed01 = Math.Clamp(MathF.Abs(forwardSpeed) / MathF.Max(maxSpeed, 1.0f), 0.0f, 1.0f);
        var steerLimit = maxSteerAngle * Lerp(1.0f, Math.Clamp(highSpeedSteerFade, 0.1f, 1.0f), speed01);
        _steerAngle = Lerp(_steerAngle, steerInput * steerLimit, 1.0f - MathF.Exp(-steerSharpness * deltaTime));

        ClampTopSpeed();
        var driveSplit = GetDriveSplit();
        UpdateDrivetrain(deltaTime, throttle, brake, forwardSpeed, driveSplit);
        var drivePower = throttle * GetEngineTorqueMultiplier();
        ApplyWheels(deltaTime, drivePower, brake, handbrake, driveSplit);
        ApplyArcadeAssists(deltaTime, steerInput, handbrake);
        UpdateTelemetry(deltaTime, throttle, brake, handbrake, steerInput, driveSplit);
        UpdateWheelVisuals(deltaTime);
    }

    private Vector3 Forward => SafeNormalize(GameObject.Forward, -Vector3.UnitZ);
    private Vector3 Right => SafeNormalize(GameObject.Right, Vector3.UnitX);
    private Vector3 Up => SafeNormalize(Vector3.Cross(Right, Forward), Vector3.UnitY);

    private void ConfigureWheel(int index, GameObject? anchor, GameObject? visual, bool steering, bool rightSide)
    {
        _wheels[index] = new WheelState
        {
            Anchor = anchor,
            Visual = visual,
            Steering = steering,
            RightSide = rightSide,
            BaseAnchorRotation = anchor?.Rotation ?? Vector3.Zero,
            BaseVisualPosition = visual?.Position ?? Vector3.Zero,
            BaseVisualRotation = visual?.Rotation ?? Vector3.Zero,
        };
    }

    private void ApplyWheels(float deltaTime, float drivePower, float brake, bool handbrake, DriveSplit driveSplit)
    {
        _groundedWheelCount = 0;
        var bodyForward = Forward;
        var bodyRight = Right;
        var bodyUp = Up;
        var rideCompression = Math.Clamp(rideHeight, 0.05f, 0.95f) * suspensionTravel;
        var rayLength = wheelRadius + suspensionTravel - rideCompression + 0.2f;

        for (var index = 0; index < _wheels.Length; ++index)
        {
            var wheel = _wheels[index];
            if (wheel.Anchor is null)
            {
                continue;
            }

            var wheelForward = bodyForward;
            var wheelRight = bodyRight;
            if (wheel.Steering)
            {
                var signedSteer = _steerAngle * MathF.Sign(NonZero(physicsSteerDirection));
                wheelForward = RotateAroundUp(bodyForward, signedSteer);
                wheelRight = RotateAroundUp(bodyRight, signedSteer);
            }

            var rayOrigin = wheel.Anchor.WorldPosition + bodyUp * 0.2f;
            var hitGround = string.IsNullOrWhiteSpace(groundTag)
                ? Physics.Raycast(rayOrigin, -bodyUp, rayLength, GameObject, out var hit)
                : Physics.RaycastTagged(rayOrigin, -bodyUp, rayLength, groundTag, GameObject, out hit);

            if (!hitGround || Vector3.Dot(hit.Normal, bodyUp) < 0.35f)
            {
                wheel.Grounded = false;
                wheel.Compression = 0.0f;
                _wheels[index] = wheel;
                continue;
            }

            _groundedWheelCount++;

            var pointVelocity = PointVelocity(wheel.Anchor.WorldPosition);
            var distanceFromAnchor = MathF.Max(hit.Distance - 0.2f, 0.0f);
            var compression = Math.Clamp(rideCompression + wheelRadius - distanceFromAnchor, 0.0f, suspensionTravel);
            var compression01 = compression / MathF.Max(suspensionTravel, 0.001f);
            var springVelocity = Vector3.Dot(pointVelocity, bodyUp);
            var suspensionForce = compression * springStrength - springVelocity * damperStrength;
            suspensionForce = Math.Clamp(suspensionForce, 0.0f, _rigidbody!.Mass * 9.81f * 0.9f);
            _rigidbody.AddForceAtPosition(bodyUp * suspensionForce, hit.Point + bodyUp * wheelRadius);

            var lateralSpeed = Vector3.Dot(pointVelocity, wheelRight);
            var forwardSpeed = Vector3.Dot(pointVelocity, wheelForward);
            var baseGrip = wheel.Steering ? frontGrip : rearGrip;
            var wheelGrip = (!wheel.Steering && handbrake) ? handbrakeGrip : baseGrip;
            var lateralForce = -wheelRight * lateralSpeed * wheelGrip * _rigidbody.Mass * 0.25f;
            var normalLoad = MathF.Max(suspensionForce, _rigidbody.Mass * 9.81f * 0.12f);
            lateralForce = ClampMagnitude(lateralForce, normalLoad * MathF.Max(gripLimit, 0.1f));

            var tireForce = lateralForce;
            var driveShare = wheel.Steering ? driveSplit.Front * 0.5f : driveSplit.Rear * 0.5f;
            if (drivePower > 0.0f && driveShare > 0.0f)
            {
                var speedLimiter = 1.0f - SmoothStep(Math.Clamp(MathF.Abs(Vector3.Dot(_rigidbody.Velocity, bodyForward)) / MathF.Max(maxSpeed, 1.0f), 0.0f, 1.0f));
                var rawDrive = wheelForward * drivePower * acceleration * _rigidbody.Mass * driveShare * speedLimiter;
                var tractionLimit = normalLoad * MathF.Max(wheelGrip, 0.0f) * MathF.Max(driveGrip, 0.0f) * driveShare;
                tireForce += ClampMagnitude(rawDrive, tractionLimit);
            }

            if (brake > 0.0f)
            {
                var reversing = MathF.Abs(forwardSpeed) < 1.2f || forwardSpeed < -0.5f;
                if (reversing && driveShare > 0.0f)
                {
                    var rawReverse = -wheelForward * brake * reverseAcceleration * _rigidbody.Mass * driveShare;
                    var tractionLimit = normalLoad * MathF.Max(wheelGrip, 0.0f) * MathF.Max(driveGrip, 0.0f) * driveShare;
                    tireForce += ClampMagnitude(rawReverse, tractionLimit);
                }
                else
                {
                    var rawBrake = -wheelForward * MathF.Sign(NonZero(forwardSpeed)) * brake * brakePower * _rigidbody.Mass * 0.25f;
                    var brakeLimit = normalLoad * MathF.Max(wheelGrip, 0.0f) * MathF.Max(brakeGrip, 0.0f);
                    tireForce += ClampMagnitude(rawBrake, brakeLimit);
                }
            }

            if (handbrake && !wheel.Steering)
            {
                tireForce += -wheelForward * MathF.Sign(NonZero(forwardSpeed)) * brakePower * _rigidbody.Mass * 0.18f;
            }

            _rigidbody.AddForceAtPosition(tireForce, hit.Point + bodyUp * 0.25f);

            wheel.Grounded = true;
            wheel.Compression = compression01;
            wheel.ForwardSpeed = forwardSpeed;
            wheel.VisualSteerAngle = wheel.Steering ? _steerAngle : 0.0f;
            _wheels[index] = wheel;
        }
    }

    private void ApplyArcadeAssists(float deltaTime, float steerInput, bool handbrake)
    {
        if (_rigidbody is null)
        {
            return;
        }

        var velocity = _rigidbody.Velocity;
        var speed = velocity.Length();
        if (speed > 0.1f)
        {
            _rigidbody.AddForce(-Vector3.UnitY * MathF.Min(downforce * speed * speed, _rigidbody.Mass * 9.81f * 2.5f));
        }

        var up = Up;
        var angularVelocity = _rigidbody.AngularVelocity;
        var uprightCorrection = Vector3.Cross(up, Vector3.UnitY) * uprightAssist;
        angularVelocity += uprightCorrection * deltaTime;

        if (_groundedWheelCount > 0)
        {
            var forwardSpeed = Vector3.Dot(velocity, Forward);
            var steerSign = steerInput * MathF.Sign(NonZero(physicsSteerDirection));
            var targetYaw = steerSign * Math.Clamp(MathF.Abs(forwardSpeed) * 0.055f, 0.0f, handbrake ? 3.0f : 1.9f);
            var yawBlend = 1.0f - MathF.Exp(-(handbrake ? driftAssist : yawAssist) * deltaTime);
            angularVelocity.Y = Lerp(angularVelocity.Y, targetYaw, yawBlend);

            if (velocity.Y > 0.0f)
            {
                _rigidbody.Velocity = new Vector3(velocity.X, velocity.Y * MathF.Exp(-2.0f * deltaTime), velocity.Z);
            }
        }

        _rigidbody.AngularVelocity = ClampMagnitude(angularVelocity, 14.0f);

        var lateralSpeed = Vector3.Dot(_rigidbody.Velocity, Right);
        var stabilityBlend = 1.0f - MathF.Exp(-MathF.Max(stabilityAssist, 0.0f) * (handbrake ? 0.25f : 1.0f) * deltaTime);
        if (stabilityBlend > 0.0f)
        {
            _rigidbody.Velocity -= Right * lateralSpeed * stabilityBlend;
        }
    }

    private DriveSplit GetDriveSplit()
    {
        return drivetrain switch
        {
            1 => new DriveSplit(1.0f, 0.0f),
            2 => new DriveSplit(0.35f, 0.65f),
            _ => new DriveSplit(0.0f, 1.0f),
        };
    }

    private void UpdateTelemetry(float deltaTime, float throttle, float brake, bool handbrake, float steerInput, DriveSplit driveSplit)
    {
        if (_rigidbody is null)
        {
            return;
        }

        var velocity = _rigidbody.Velocity;
        var forwardSpeed = Vector3.Dot(velocity, Forward);
        var lateralSpeed = Vector3.Dot(velocity, Right);
        var groundedDrivenWheelSpeed = GetAverageDrivenWheelSpeed(driveSplit, forwardSpeed);
        var longitudinalSlip = EstimateLongitudinalSlip(throttle, brake, driveSplit);
        var wheelRpm = WheelRpm(_drivenWheelSpinSpeed);

        var speed = velocity.Length();
        var slipRatio = MathF.Abs(lateralSpeed) / MathF.Max(MathF.Abs(forwardSpeed), 1.0f);
        var wheelSpinSlip = MathF.Max(MathF.Abs(_drivenWheelSpinSpeed) - MathF.Abs(forwardSpeed), 0.0f);
        longitudinalSlip = MathF.Max(longitudinalSlip, wheelSpinSlip / MathF.Max(MathF.Abs(groundedDrivenWheelSpeed), 8.0f));
        var smokeAmount = Math.Clamp(MathF.Max(slipRatio - 0.25f, MathF.Max(longitudinalSlip, wheelSpinSlip / 12.0f)) / 0.75f, 0.0f, 1.0f);
        if (speed < 2.0f)
        {
            smokeAmount *= Math.Clamp(speed / 2.0f + throttle * 0.5f, 0.0f, 1.0f);
        }

        s_telemetryByEntity[EntityId] = new VehicleTelemetry(
            speed,
            forwardSpeed,
            lateralSpeed,
            _drivenWheelSpinSpeed,
            wheelRpm,
            _engineRpm,
            Math.Clamp(_engineRpm / MathF.Max(redlineRpm, idleRpm + 100.0f), 0.0f, 1.0f),
            _currentGear,
            throttle,
            brake,
            steerInput,
            handbrake,
            _groundedWheelCount,
            slipRatio,
            longitudinalSlip,
            smokeAmount,
            drivetrain,
            driveSplit.Front,
            driveSplit.Rear);
    }

    private float GetAverageDrivenWheelSpeed(DriveSplit driveSplit, float fallbackSpeed)
    {
        var speedSum = 0.0f;
        var weightSum = 0.0f;
        foreach (var wheel in _wheels)
        {
            var driveWeight = wheel.Steering ? driveSplit.Front : driveSplit.Rear;
            if (!wheel.Grounded || driveWeight <= 0.0f)
            {
                continue;
            }

            speedSum += wheel.ForwardSpeed * driveWeight;
            weightSum += driveWeight;
        }

        return weightSum > 0.0001f ? speedSum / weightSum : fallbackSpeed;
    }

    private float EstimateLongitudinalSlip(float throttle, float brake, DriveSplit driveSplit)
    {
        var slip = 0.0f;
        foreach (var wheel in _wheels)
        {
            if (!wheel.Grounded)
            {
                continue;
            }

            var driveWeight = wheel.Steering ? driveSplit.Front : driveSplit.Rear;
            var wheelGrip = wheel.Steering ? frontGrip : rearGrip;
            var lowGripAmount = 1.0f - Math.Clamp(wheelGrip / 4.0f, 0.0f, 1.0f);
            slip = MathF.Max(slip, throttle * driveWeight * lowGripAmount);
            slip = MathF.Max(slip, brake * lowGripAmount * 0.35f);
        }

        return Math.Clamp(slip, 0.0f, 1.0f);
    }

    private void UpdateDrivetrain(float deltaTime, float throttle, float brake, float forwardSpeed, DriveSplit driveSplit)
    {
        if (_shiftTimer > 0.0f)
        {
            _shiftTimer = MathF.Max(_shiftTimer - deltaTime, 0.0f);
        }

        if (forwardSpeed < -0.5f)
        {
            _currentGear = -1;
        }
        else if (_currentGear <= 0)
        {
            _currentGear = 1;
        }

        var drivenSpeed = GetAverageDrivenWheelSpeed(driveSplit, forwardSpeed);
        var mechanicalRpm = EstimateRpmForGear(MathF.Abs(drivenSpeed), _currentGear);
        if (_shiftTimer <= 0.0f && _currentGear > 0)
        {
            if (_currentGear < 6 && mechanicalRpm >= MathF.Max(upshiftRpm, idleRpm + 500.0f))
            {
                _currentGear++;
                _shiftTimer = MathF.Max(shiftDuration, 0.0f);
                mechanicalRpm = EstimateRpmForGear(MathF.Abs(drivenSpeed), _currentGear);
            }
            else if (_currentGear > 1 && mechanicalRpm <= MathF.Max(downshiftRpm, idleRpm))
            {
                _currentGear--;
                _shiftTimer = MathF.Max(shiftDuration * 0.45f, 0.0f);
                mechanicalRpm = EstimateRpmForGear(MathF.Abs(drivenSpeed), _currentGear);
            }
        }

        var targetRpm = MathF.Max(mechanicalRpm, MathF.Max(idleRpm, 0.0f));
        if (throttle > 0.0f && MathF.Abs(forwardSpeed) < 2.0f)
        {
            targetRpm = MathF.Max(targetRpm, Lerp(MathF.Max(idleRpm, 0.0f), MathF.Max(launchRpm, idleRpm), throttle));
        }
        else if (throttle > 0.0f)
        {
            targetRpm += throttle * 350.0f;
        }

        if (brake > 0.0f && _currentGear == -1)
        {
            targetRpm = MathF.Max(targetRpm, Lerp(MathF.Max(idleRpm, 0.0f), MathF.Max(launchRpm, idleRpm), brake * 0.65f));
        }

        targetRpm = Math.Clamp(targetRpm, MathF.Max(idleRpm, 0.0f), MathF.Max(redlineRpm, idleRpm + 100.0f));
        var response = _shiftTimer > 0.0f ? engineResponse * 1.8f : engineResponse;
        _engineRpm = Lerp(_engineRpm, targetRpm, 1.0f - MathF.Exp(-MathF.Max(response, 0.0f) * deltaTime));
        UpdateDrivenWheelSpinSpeed(deltaTime, throttle, brake, forwardSpeed, drivenSpeed);
    }

    private float GetEngineTorqueMultiplier()
    {
        if (_shiftTimer > 0.0f)
        {
            return 0.18f;
        }

        var idle = MathF.Max(idleRpm, 0.0f);
        var redline = MathF.Max(redlineRpm, idle + 100.0f);
        var peak = Math.Clamp(peakTorqueRpm, idle + 100.0f, redline);
        var lowTorque = Lerp(0.28f, 1.0f, SmoothStep(InverseLerp(idle, peak, _engineRpm)));
        var highRpmFade = Lerp(1.0f, 0.72f, SmoothStep(InverseLerp(peak, redline, _engineRpm)));
        return Math.Clamp(lowTorque * highRpmFade, 0.0f, 1.0f);
    }

    private void UpdateDrivenWheelSpinSpeed(float deltaTime, float throttle, float brake, float forwardSpeed, float groundedDrivenSpeed)
    {
        var driveInput = throttle > 0.0f || (brake > 0.0f && _currentGear == -1);
        var targetWheelSpeed = groundedDrivenSpeed;
        if (driveInput)
        {
            targetWheelSpeed = EngineRpmToWheelSpeed(_engineRpm, _currentGear);
            if (_currentGear > 0 && targetWheelSpeed < 0.0f)
            {
                targetWheelSpeed = MathF.Abs(targetWheelSpeed);
            }
            else if (_currentGear == -1 && targetWheelSpeed > 0.0f)
            {
                targetWheelSpeed = -targetWheelSpeed;
            }

            if (MathF.Abs(forwardSpeed) > 0.5f && MathF.Sign(targetWheelSpeed) != MathF.Sign(forwardSpeed) && _currentGear > 0)
            {
                targetWheelSpeed = MathF.Abs(targetWheelSpeed) * MathF.Sign(forwardSpeed);
            }
        }

        var response = driveInput ? 18.0f : 10.0f;
        _drivenWheelSpinSpeed = Lerp(_drivenWheelSpinSpeed, targetWheelSpeed, 1.0f - MathF.Exp(-response * deltaTime));
    }

    private float EstimateRpmForGear(float speed, int gear)
    {
        var rpm = MathF.Abs(WheelRpm(speed)) * MathF.Abs(GetGearRatio(gear)) * MathF.Max(finalDriveRatio, 0.01f);
        return Math.Clamp(rpm, MathF.Max(idleRpm, 0.0f), MathF.Max(redlineRpm, idleRpm + 100.0f));
    }

    private float EngineRpmToWheelSpeed(float rpm, int gear)
    {
        var ratio = MathF.Abs(GetGearRatio(gear)) * MathF.Max(finalDriveRatio, 0.01f);
        if (ratio <= 0.0001f)
        {
            return 0.0f;
        }

        var wheelRpm = MathF.Abs(rpm) / ratio;
        var circumference = 2.0f * MathF.PI * MathF.Max(wheelRadius, 0.01f);
        var speed = wheelRpm * circumference / 60.0f;
        return gear < 0 ? -speed : speed;
    }

    private float WheelRpm(float speed)
    {
        var circumference = 2.0f * MathF.PI * MathF.Max(wheelRadius, 0.01f);
        return speed / circumference * 60.0f;
    }

    private float GetGearRatio(int gear)
    {
        return gear switch
        {
            -1 => MathF.Max(reverseGearRatio, 0.01f),
            1 => MathF.Max(firstGearRatio, 0.01f),
            2 => MathF.Max(secondGearRatio, 0.01f),
            3 => MathF.Max(thirdGearRatio, 0.01f),
            4 => MathF.Max(fourthGearRatio, 0.01f),
            5 => MathF.Max(fifthGearRatio, 0.01f),
            _ => MathF.Max(sixthGearRatio, 0.01f),
        };
    }

    private void ClampTopSpeed()
    {
        if (_rigidbody is null)
        {
            return;
        }

        var limit = MathF.Max(maxSpeed, 1.0f) * 1.25f;
        if (_rigidbody.Velocity.LengthSquared() > limit * limit)
        {
            _rigidbody.Velocity = Vector3.Normalize(_rigidbody.Velocity) * limit;
        }
    }

    private Vector3 PointVelocity(Vector3 worldPoint)
    {
        if (_rigidbody is null)
        {
            return Vector3.Zero;
        }

        return _rigidbody.Velocity + Vector3.Cross(_rigidbody.AngularVelocity, worldPoint - GameObject.WorldPosition);
    }

    private void RecoverCar()
    {
        if (_rigidbody is null)
        {
            return;
        }

        var rotation = GameObject.Rotation;
        GameObject.Rotation = new Vector3(0.0f, rotation.Y, 0.0f);
        _rigidbody.AngularVelocity = Vector3.Zero;
        _rigidbody.Velocity = new Vector3(_rigidbody.Velocity.X, 0.0f, _rigidbody.Velocity.Z);
        _rigidbody.AddImpulse(Vector3.UnitY * _rigidbody.Mass * 4.0f);
    }

    private void UpdateWheelVisuals(float deltaTime)
    {
        for (var index = 0; index < _wheels.Length; ++index)
        {
            var wheel = _wheels[index];
            if (wheel.Visual is null)
            {
                continue;
            }

            if (wheel.Steering && steerFrontWheelAnchors && wheel.Anchor is not null)
            {
                wheel.Anchor.Rotation = wheel.BaseAnchorRotation + wheelSteerAxis * wheel.VisualSteerAngle;
            }

            var localRise = wheel.Compression * suspensionTravel - rideHeight * suspensionTravel;
            wheel.Visual.Position = new Vector3(
                wheel.BaseVisualPosition.X,
                wheel.BaseVisualPosition.Y + localRise,
                wheel.BaseVisualPosition.Z);

            var spinDirection = wheel.RightSide ? rightWheelSpinDirection : leftWheelSpinDirection;
            wheel.SpinDegrees += wheel.ForwardSpeed / MathF.Max(wheelRadius, 0.001f) * deltaTime * 180.0f / MathF.PI * spinDirection;

            var visualRotation = wheel.BaseVisualRotation + wheelSpinAxis * wheel.SpinDegrees;
            if (wheel.Steering && !steerFrontWheelAnchors)
            {
                visualRotation += wheelSteerAxis * wheel.VisualSteerAngle;
            }

            wheel.Visual.Rotation = visualRotation;
            _wheels[index] = wheel;
        }
    }

    private static Vector3 RotateAroundUp(Vector3 vector, float degrees)
    {
        var radians = degrees * MathF.PI / 180.0f;
        var sin = MathF.Sin(radians);
        var cos = MathF.Cos(radians);
        return new Vector3(
            vector.X * cos - vector.Z * sin,
            vector.Y,
            vector.X * sin + vector.Z * cos);
    }

    private static Vector3 SafeNormalize(Vector3 value, Vector3 fallback)
    {
        return value.LengthSquared() > 0.000001f ? Vector3.Normalize(value) : fallback;
    }

    private static Vector3 ClampMagnitude(Vector3 value, float maxLength)
    {
        if (maxLength <= 0.0f)
        {
            return Vector3.Zero;
        }

        return value.LengthSquared() > maxLength * maxLength
            ? Vector3.Normalize(value) * maxLength
            : value;
    }

    private static float Lerp(float from, float to, float amount)
    {
        return from + (to - from) * Math.Clamp(amount, 0.0f, 1.0f);
    }

    private static float SmoothStep(float value)
    {
        var t = Math.Clamp(value, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    private static float InverseLerp(float from, float to, float value)
    {
        if (MathF.Abs(to - from) <= 0.0001f)
        {
            return value >= to ? 1.0f : 0.0f;
        }

        return Math.Clamp((value - from) / (to - from), 0.0f, 1.0f);
    }

    private static float NonZero(float value)
    {
        return MathF.Abs(value) < 0.1f ? 1.0f : value;
    }

    private struct WheelState
    {
        public GameObject? Anchor;
        public GameObject? Visual;
        public bool Steering;
        public bool RightSide;
        public bool Grounded;
        public float Compression;
        public float ForwardSpeed;
        public float VisualSteerAngle;
        public float SpinDegrees;
        public Vector3 BaseAnchorRotation;
        public Vector3 BaseVisualPosition;
        public Vector3 BaseVisualRotation;
    }

    private readonly struct DriveSplit
    {
        public DriveSplit(float front, float rear)
        {
            Front = front;
            Rear = rear;
        }

        public float Front { get; }
        public float Rear { get; }
    }
}

public readonly struct VehicleTelemetry
{
    public VehicleTelemetry(
        float speed,
        float forwardSpeed,
        float lateralSpeed,
        float drivenWheelSpeed,
        float drivenWheelRpm,
        float engineRpm,
        float engineRpm01,
        int gear,
        float throttle,
        float brake,
        float steering,
        bool handbrake,
        int groundedWheelCount,
        float lateralSlip,
        float longitudinalSlip,
        float tyreSmoke,
        int drivetrain,
        float frontDriveShare,
        float rearDriveShare)
    {
        Speed = speed;
        ForwardSpeed = forwardSpeed;
        LateralSpeed = lateralSpeed;
        DrivenWheelSpeed = drivenWheelSpeed;
        DrivenWheelRpm = drivenWheelRpm;
        EngineRpm = engineRpm;
        EngineRpm01 = engineRpm01;
        Gear = gear;
        Throttle = throttle;
        Brake = brake;
        Steering = steering;
        Handbrake = handbrake;
        GroundedWheelCount = groundedWheelCount;
        LateralSlip = lateralSlip;
        LongitudinalSlip = longitudinalSlip;
        TyreSmoke = tyreSmoke;
        Drivetrain = drivetrain;
        FrontDriveShare = frontDriveShare;
        RearDriveShare = rearDriveShare;
    }

    public float Speed { get; }
    public float ForwardSpeed { get; }
    public float LateralSpeed { get; }
    public float DrivenWheelSpeed { get; }
    public float DrivenWheelRpm { get; }
    public float EngineRpm { get; }
    public float EngineRpm01 { get; }
    public int Gear { get; }
    public float Throttle { get; }
    public float Brake { get; }
    public float Steering { get; }
    public bool Handbrake { get; }
    public int GroundedWheelCount { get; }
    public float LateralSlip { get; }
    public float LongitudinalSlip { get; }
    public float TyreSmoke { get; }
    public int Drivetrain { get; }
    public float FrontDriveShare { get; }
    public float RearDriveShare { get; }
    public bool IsGrounded => GroundedWheelCount > 0;
}
