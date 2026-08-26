using System;
using System.Collections.Generic;
using System.Numerics;
using System.Text;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Raycast vehicle controller tuned for weighty, compliant arcade handling.
/// </summary>
public sealed class RaycastVehicleController : ScriptBehaviour
{
    [SerializedField] private RaycastVehicleSettings? vehicleSettings = null;

    [SerializedField] private GameObject? frontLeftWheelAnchor = null;
    [SerializedField] private GameObject? frontRightWheelAnchor = null;
    [SerializedField] private GameObject? rearLeftWheelAnchor = null;
    [SerializedField] private GameObject? rearRightWheelAnchor = null;

    [SerializedField] private GameObject? frontLeftWheelVisual = null;
    [SerializedField] private GameObject? frontRightWheelVisual = null;
    [SerializedField] private GameObject? rearLeftWheelVisual = null;
    [SerializedField] private GameObject? rearRightWheelVisual = null;
    [SerializedField] private GameObject? centerOfMass = null;

    [SerializedField] private float mass    = 1250.0f;
    [SerializedField] private float wheelRadius    = 0.5f;
    [SerializedField] private float drivenWheelInertia = 1.5f;
    [SerializedField] private float suspensionTravel    = 0.32f;
    [SerializedField] private float rideHeight    = 0.42f;
    [SerializedField] private float springStrength    = 24000.0f;
    [SerializedField] private float damperStrength    = 3000.0f;
    [SerializedField] private float visualSuspensionSharpness = 12.0f;
    [SerializedField] private float visualSuspensionDeadZone = 0.003f;

    [SerializedField] private float acceleration    = 40.0f;
    [SerializedField] private float throttleResponse = 4.0f;
    [SerializedField] private float reverseAcceleration    = 14.0f;
    [SerializedField] private float brakePower    = 15.0f;
    [SerializedField] private float maxSpeed    = 92.0f;
    [SerializedField] private int drivetrain    = 0;
    [SerializedField] private float frontGrip    = 6.0f;
    [SerializedField] private float rearGrip    = 5.0f;
    [SerializedField] private float gripLimit    = 1.5f;
    [SerializedField] private float driveGrip    = 1.0f;
    [SerializedField] private float peakLongitudinalSlip = 0.12f;
    [SerializedField] private float fullLongitudinalSlip = 1.0f;
    [SerializedField] private float spinningTyreLongitudinalGrip = 0.55f;
    [SerializedField] private float spinningTyreLateralGrip = 0.15f;
    [SerializedField] private float brakeGrip    = 1.0f;
    [SerializedField] private float handbrakeGrip    = 0.45f;
    [SerializedField] private float airDensity = 1.225f;
    [SerializedField] private float downforceCoefficient = 1.2f;
    [SerializedField] private float downforceArea = 2.2f;
    [SerializedField] private float frontDownforceBalance = 0.45f;
    [SerializedField] private float idleRpm  = 900.0f;
    [SerializedField] private float redlineRpm  = 7200.0f;
    [SerializedField] private float finalDriveRatio  = 4.5f;
    [SerializedField] private float reverseGearRatio  = 3.20000005f;
    [SerializedField] private float firstGearRatio  = 4.0f;
    [SerializedField] private float secondGearRatio  = 2.0f;
    [SerializedField] private float thirdGearRatio  = 1.0f;
    [SerializedField] private float fourthGearRatio  = 0.5f;
    [SerializedField] private float fifthGearRatio  = 0.300000012f;
    [SerializedField] private float sixthGearRatio  = 0.25f;
    [SerializedField] private float upshiftRpm  = 6800.0f;
    [SerializedField] private float downshiftRpm  = 1200.0f;
    [SerializedField] private float shiftHysteresisRpm = 250.0f;
    [SerializedField] private float engineResponse  = 20.0f;
    [SerializedField] private float launchRpm  = 3400.0f;
    [SerializedField] private float peakTorqueRpm  = 4800.0f;
    [SerializedField] private float shiftDuration  = 0.5f;

    [SerializedField] private float maxSteerAngle    = 32.0f;
    [SerializedField] private float steerSharpness    = 5.0f;
    [SerializedField] private float highSpeedSteerFade    = 0.12f;
    [SerializedField] private bool steerFrontWheelAnchors    = true;
    [SerializedField] private float physicsSteerDirection    = -1.0f;
    [SerializedField] private Vector3 wheelSpinAxis    = new Vector3(-1.0f, 0.0f, 0.0f);
    [SerializedField] private Vector3 wheelSteerAxis    = new Vector3(0.0f, 1.0f, 0.0f);
    [SerializedField] private float leftWheelSpinDirection    = 1.0f;
    [SerializedField] private float rightWheelSpinDirection    = 1.0f;

    [SerializedField] private bool lockCursorOnCreate    = true;
    [SerializedField, InputMappingAsset] private string inputMappingAsset = "";
    [SerializedField] private string groundTag    = "ground";
    [SerializedField] private bool suspensionDiagnostics = true;

    private readonly WheelState[] _wheels = new WheelState[4];
    private static readonly Dictionary<uint, VehicleTelemetry> s_telemetryByEntity = [];
    private RigidbodyComponent? _rigidbody;
    private float _steerAngle;
    private float _engineRpm;
    private float _drivenWheelSpinSpeed;
    private float _shiftTimer;
    private int _currentGear = 1;
    private int _groundedWheelCount;
    private float _throttleInput;
    private float _driveThrottle;
    private float _brakeInput;
    private float _steerInput;
    private bool _handbrakeInput;
    private int _diagnosticStep;
    private InputActionMap? _inputActions;

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
        if (!string.IsNullOrWhiteSpace(inputMappingAsset))
        {
            try { _inputActions = InputActionMap.Load(inputMappingAsset); }
            catch (Exception exception) { Debug.LogError($"Unable to load vehicle input map '{inputMappingAsset}': {exception.Message}"); }
        }
        UpgradeLegacySuspensionPreset();
        ApplyVehicleSettings();

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
            _rigidbody.CenterOfMass = centerOfMass?.Position ?? Vector3.Zero;
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

    private void ApplyVehicleSettings()
    {
        if (vehicleSettings is null)
        {
            return;
        }

        mass = vehicleSettings.Mass;
        wheelRadius = vehicleSettings.WheelRadius;
        drivenWheelInertia = vehicleSettings.DrivenWheelInertia;
        suspensionTravel = vehicleSettings.SuspensionTravel;
        rideHeight = vehicleSettings.RideHeight;
        springStrength = vehicleSettings.SpringStrength;
        damperStrength = vehicleSettings.DamperStrength;

        acceleration = vehicleSettings.Acceleration;
        throttleResponse = vehicleSettings.ThrottleResponse;
        reverseAcceleration = vehicleSettings.ReverseAcceleration;
        brakePower = vehicleSettings.BrakePower;
        maxSpeed = vehicleSettings.MaxSpeed;
        drivetrain = vehicleSettings.Drivetrain;

        frontGrip = vehicleSettings.FrontGrip;
        rearGrip = vehicleSettings.RearGrip;
        gripLimit = vehicleSettings.GripLimit;
        driveGrip = vehicleSettings.DriveGrip;
        peakLongitudinalSlip = vehicleSettings.PeakLongitudinalSlip;
        fullLongitudinalSlip = vehicleSettings.FullLongitudinalSlip;
        spinningTyreLongitudinalGrip = vehicleSettings.SpinningTyreLongitudinalGrip;
        spinningTyreLateralGrip = vehicleSettings.SpinningTyreLateralGrip;
        brakeGrip = vehicleSettings.BrakeGrip;
        handbrakeGrip = vehicleSettings.HandbrakeGrip;

        airDensity = vehicleSettings.AirDensity;
        downforceCoefficient = vehicleSettings.DownforceCoefficient;
        downforceArea = vehicleSettings.DownforceArea;
        frontDownforceBalance = vehicleSettings.FrontDownforceBalance;

        idleRpm = vehicleSettings.IdleRpm;
        redlineRpm = vehicleSettings.RedlineRpm;
        finalDriveRatio = vehicleSettings.FinalDriveRatio;
        reverseGearRatio = vehicleSettings.ReverseGearRatio;
        firstGearRatio = vehicleSettings.FirstGearRatio;
        secondGearRatio = vehicleSettings.SecondGearRatio;
        thirdGearRatio = vehicleSettings.ThirdGearRatio;
        fourthGearRatio = vehicleSettings.FourthGearRatio;
        fifthGearRatio = vehicleSettings.FifthGearRatio;
        sixthGearRatio = vehicleSettings.SixthGearRatio;
        upshiftRpm = vehicleSettings.UpshiftRpm;
        downshiftRpm = vehicleSettings.DownshiftRpm;
        shiftHysteresisRpm = vehicleSettings.ShiftHysteresisRpm;
        engineResponse = vehicleSettings.EngineResponse;
        launchRpm = vehicleSettings.LaunchRpm;
        peakTorqueRpm = vehicleSettings.PeakTorqueRpm;
        shiftDuration = vehicleSettings.ShiftDuration;

        maxSteerAngle = vehicleSettings.MaxSteerAngle;
        steerSharpness = vehicleSettings.SteerSharpness;
        highSpeedSteerFade = vehicleSettings.HighSpeedSteerFade;

        if (_rigidbody is not null)
        {
            _rigidbody.Mass = MathF.Max(mass, 50.0f);
        }
    }

    private void UpgradeLegacySuspensionPreset()
    {
        // Migrate the short-lived 0.34 m default used by an earlier controller
        // revision; this vehicle's wheel mesh is authored for a 0.5 m radius.
        if (wheelRadius >= 0.33f && wheelRadius <= 0.35f)
        {
            wheelRadius = 0.5f;
        }
        if (springStrength >= 27000.0f && springStrength <= 29000.0f &&
            damperStrength >= 4900.0f && damperStrength <= 5300.0f)
        {
            springStrength = 24000.0f;
            damperStrength = 3000.0f;
        }
        if (highSpeedSteerFade >= 0.39f && highSpeedSteerFade <= 0.41f)
        {
            highSpeedSteerFade = 0.12f;
        }
        // The original handbrake preset was still high enough to reach the
        // generic tyre-force cap, so pulling it barely changed rear grip.
        if (handbrakeGrip >= 1.99f && handbrakeGrip <= 2.01f)
        {
            handbrakeGrip = 0.45f;
        }
        // Existing scene components retain serialized field values when script
        // defaults change. Migrate only the original rigid preset, leaving any
        // deliberately customized suspension untouched.
        var hasLegacyPreset = suspensionTravel <= 0.11f &&
                              springStrength >= 100000.0f &&
                              damperStrength >= 10000.0f;
        if (!hasLegacyPreset)
        {
            return;
        }

        suspensionTravel = 0.32f;
        rideHeight = 0.42f;
        springStrength = 24000.0f;
        damperStrength = 3000.0f;
    }

    public override void OnUpdate(float deltaTime)
    {
        ApplyVehicleSettings();

        if (deltaTime <= 0.0f || _rigidbody is null)
        {
            return;
        }

        if (!GamePause.IsPaused && Input.IsKeyPressed(KeyCode.Escape))
        {
            Input.CursorLocked = !Input.CursorLocked;
        }

        if (_inputActions?.WasPressed("Recover") ?? Input.IsKeyPressed(KeyCode.R))
        {
            RecoverCar();
        }

        _throttleInput = _inputActions?.GetAxis("Throttle") ?? (Input.IsKeyDown(KeyCode.W) ? 1.0f : 0.0f);
        _brakeInput = _inputActions?.GetAxis("Brake") ?? (Input.IsKeyDown(KeyCode.S) ? 1.0f : 0.0f);
        _handbrakeInput = _inputActions?.IsDown("Handbrake") ?? Input.IsKeyDown(KeyCode.Space);
        _steerInput = _inputActions?.GetAxis("Steer") ??
            ((Input.IsKeyDown(KeyCode.A) ? 1.0f : 0.0f) - (Input.IsKeyDown(KeyCode.D) ? 1.0f : 0.0f));

        var forwardSpeed = Vector3.Dot(_rigidbody.Velocity, Forward);
        var absoluteSpeed = MathF.Abs(forwardSpeed);
        var minimumSteerRatio = Math.Clamp(highSpeedSteerFade, 0.05f, 1.0f);
        // A reciprocal steering rack gives useful lock while parking but falls
        // rapidly to a few degrees at road speed. Scaling only by maxSpeed left
        // far too much lock available through most of the vehicle's range.
        var speedSteerRatio = 1.0f / (1.0f + absoluteSpeed * 0.14f);
        var steerRatio = MathF.Max(minimumSteerRatio, speedSteerRatio);
        var accelerationSteerReduction = Lerp(1.0f, 0.72f,
            _throttleInput * Math.Clamp(absoluteSpeed / 25.0f, 0.0f, 1.0f));
        var steerLimit = maxSteerAngle * steerRatio * accelerationSteerReduction;
        _steerAngle = Lerp(_steerAngle, _steerInput * steerLimit, 1.0f - MathF.Exp(-steerSharpness * deltaTime));
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (fixedDeltaTime <= 0.0f || _rigidbody is null)
        {
            return;
        }

        var forwardSpeed = Vector3.Dot(_rigidbody.Velocity, Forward);
        var throttleBlend = 1.0f - MathF.Exp(-MathF.Max(throttleResponse, 0.1f) * fixedDeltaTime);
        _driveThrottle = Lerp(_driveThrottle, _throttleInput, throttleBlend);
        if (_throttleInput > 0.0f || _brakeInput > 0.0f || _handbrakeInput || MathF.Abs(_steerInput) > 0.0f)
        {
            // A zero-magnitude discrete impulse is an explicit wake request.
            // Continuous tire and suspension forces intentionally do not wake a
            // body that Bullet has put to sleep.
            _rigidbody.AddImpulse(Vector3.Zero);
        }
        var driveSplit = GetDriveSplit();
        UpdateDrivetrain(fixedDeltaTime, _throttleInput, _brakeInput, forwardSpeed, driveSplit);
        // When the requested drive direction opposes the car's motion, engine
        // force must remain available to bring it through zero speed. Applying
        // the rev limiter to wheel speed from the old direction could otherwise
        // leave the car permanently unable to change direction.
        var changingToForward = _throttleInput > 0.0f && forwardSpeed < -0.5f;
        var drivePower = _driveThrottle * (changingToForward ? 1.0f : GetEngineTorqueMultiplier());
        ApplyAerodynamicDownforce();
        ApplyWheels(fixedDeltaTime, drivePower, _brakeInput, _handbrakeInput, driveSplit);
    }

    public override void OnLateUpdate(float deltaTime)
    {
        // Scene physics runs between OnUpdate and OnLateUpdate. Wheel transforms
        // must be written here; otherwise the physics-driven parent transform
        // moves them immediately after placement and they snap back next frame.
        var driveSplit = GetDriveSplit();
        UpdateTelemetry(deltaTime, _throttleInput, _brakeInput, _handbrakeInput, _steerInput, driveSplit);
        UpdateWheelVisuals(deltaTime, driveSplit);
    }

    private Vector3 Forward => SafeNormalize(GameObject.Forward, -Vector3.UnitZ);
    private Vector3 Right => SafeNormalize(GameObject.Right, Vector3.UnitX);
    private Vector3 Up => SafeNormalize(Vector3.Cross(Right, Forward), Vector3.UnitY);

    private void ConfigureWheel(int index, GameObject? anchor, GameObject? visual, bool steering, bool rightSide)
    {
        var restCompression01 = 1.0f - Math.Clamp(rideHeight, 0.05f, 0.95f);
        _wheels[index] = new WheelState
        {
            Anchor = anchor,
            Visual = visual,
            Steering = steering,
            RightSide = rightSide,
            BaseAnchorRotation = anchor?.Rotation ?? Vector3.Zero,
            BaseVisualPosition = visual?.Position ?? Vector3.Zero,
            BaseVisualRotation = visual?.Rotation ?? Vector3.Zero,
            VisualCompression = restCompression01,
        };
    }

    private void ApplyWheels(float deltaTime, float drivePower, float brake, bool handbrake, DriveSplit driveSplit)
    {
        _groundedWheelCount = 0;
        var bodyForward = Forward;
        var bodyRight = Right;
        var bodyUp = Up;
        var chassisForwardSpeed = Vector3.Dot(_rigidbody!.Velocity, bodyForward);
        // S is a service brake while travelling forwards, then becomes reverse
        // once the car is nearly stopped. Decide that once for the whole car:
        // using each steered wheel's local speed made one axle brake while the
        // driven axle was trying to reverse.
        var reverseRequested = brake > 0.0f && _throttleInput <= 0.0f && chassisForwardSpeed < 1.2f;
        var travel = MathF.Max(suspensionTravel, 0.01f);
        // rideHeight is the desired extension fraction: 0 is near full bump,
        // 1 is near full droop. Spring compression is its inverse.
        var restCompression = (1.0f - Math.Clamp(rideHeight, 0.05f, 0.95f)) * travel;
        // Start above the entire wheel/travel range. A short ray starting at the
        // anchor loses the floor as soon as a hard landing drives that anchor
        // below the surface, leaving the suspension unable to recover.
        var rayOriginOffset = wheelRadius + travel + 0.25f;
        var rayLength = rayOriginOffset + wheelRadius + travel + 0.10f;
        var emitDiagnostics = suspensionDiagnostics && (++_diagnosticStep % 12 == 0);
        // Use the live pedal state here rather than smoothed drive power.
        // Residual throttle blending after releasing W must not suppress
        // reverse when the driver subsequently presses S.
        var burnout = _throttleInput > 0.0f && brake > 0.0f &&
                      MathF.Abs(chassisForwardSpeed) < 8.0f;
        var drivenWheelSurfaceAcceleration = 0.0f;
        StringBuilder? diagnostics = emitDiagnostics
            ? new StringBuilder($"[VehicleSuspension] bodyY={GameObject.WorldPosition.Y:F6} vy={_rigidbody!.Velocity.Y:F6} " +
                                $"wx={_rigidbody.AngularVelocity.X:F6} wz={_rigidbody.AngularVelocity.Z:F6} " +
                                $"rot=({GameObject.WorldRotation.X:F4},{GameObject.WorldRotation.Y:F4},{GameObject.WorldRotation.Z:F4})")
            : null;

        for (var index = 0; index < _wheels.Length; ++index)
        {
            var wheel = _wheels[index];
            if (wheel.Anchor is null)
            {
                continue;
            }

            var anchorWorldPosition = wheel.Anchor.WorldPosition;

            var wheelForward = bodyForward;
            var wheelRight = bodyRight;
            if (wheel.Steering)
            {
                var signedSteer = _steerAngle * MathF.Sign(NonZero(physicsSteerDirection));
                wheelForward = RotateAroundUp(bodyForward, signedSteer);
                wheelRight = RotateAroundUp(bodyRight, signedSteer);
            }

            var rayOrigin = anchorWorldPosition + bodyUp * rayOriginOffset;
            var hitGround = string.IsNullOrWhiteSpace(groundTag)
                ? Physics.Raycast(rayOrigin, -bodyUp, rayLength, GameObject, out var hit)
                : Physics.RaycastTagged(rayOrigin, -bodyUp, rayLength, groundTag, GameObject, out hit);

            if (!hitGround || Vector3.Dot(hit.Normal, bodyUp) < 0.35f)
            {
                diagnostics?.Append($" | w{index}:MISS");
                wheel.Grounded = false;
                wheel.Compression = 0.0f;
                _wheels[index] = wheel;
                continue;
            }

            _groundedWheelCount++;

            var pointVelocity = _rigidbody!.GetVelocityAtPoint(anchorWorldPosition);
            // Keep this distance signed. A negative value means the anchor has
            // crossed the contact plane and must feed a bump stop, not turn the
            // wheel into a zero-distance ordinary suspension sample.
            var distanceFromAnchor = Vector3.Dot(anchorWorldPosition - hit.Point, bodyUp);
            // Compression is measured over the complete suspension range. The
            // old formula substituted the desired ride compression for travel;
            // that made ride height cancel out of the equilibrium geometry.
            var rawCompression = wheelRadius + travel - distanceFromAnchor;
            var compression = Math.Clamp(rawCompression, 0.0f, travel);
            var bottomOut = MathF.Max(rawCompression - travel, 0.0f);
            var compression01 = compression / travel;
            var suspensionPointSpeed = Vector3.Dot(pointVelocity, bodyUp);
            var cornerWeight = _rigidbody!.Mass * 9.81f * 0.25f;
            var springDisplacement = compression - restCompression;
            var suspensionForce = cornerWeight +
                                  springDisplacement * MathF.Max(springStrength, 0.0f) +
                                  -suspensionPointSpeed * MathF.Max(damperStrength, 0.0f) +
                                  bottomOut * MathF.Max(springStrength, 0.0f) * 4.0f;
            suspensionForce = Math.Clamp(suspensionForce, 0.0f, cornerWeight * 12.0f);
            diagnostics?.Append($" | w{index}:id={hit.Entity.EntityId},ay={anchorWorldPosition.Y:F6}," +
                                $"hy={hit.Point.Y:F6},ny={hit.Normal.Y:F6},d={distanceFromAnchor:F6}," +
                                $"c={compression:F6},b={bottomOut:F6},pv={suspensionPointSpeed:F6},f={suspensionForce:F2}");
            _rigidbody.AddForceAtPosition(bodyUp * suspensionForce, hit.Point + bodyUp * wheelRadius);

            var lateralSpeed = Vector3.Dot(pointVelocity, wheelRight);
            var forwardSpeed = Vector3.Dot(pointVelocity, wheelForward);
            var baseGrip = wheel.Steering ? frontGrip : rearGrip;
            var wheelGrip = (!wheel.Steering && handbrake) ? handbrakeGrip : baseGrip;
            var lateralForce = -wheelRight * lateralSpeed * wheelGrip * _rigidbody.Mass * 0.25f;
            var normalLoad = MathF.Max(suspensionForce, _rigidbody.Mass * 9.81f * 0.12f);
            var lateralGripRatio = MathF.Max(wheelGrip, 0.0f) / MathF.Max(baseGrip, 0.01f);
            lateralForce = ClampMagnitude(lateralForce,
                normalLoad * MathF.Max(gripLimit, 0.1f) * lateralGripRatio);

            var tireForce = lateralForce;
            var driveShare = wheel.Steering ? driveSplit.Front * 0.5f : driveSplit.Rear * 0.5f;
            var drivenWheel = driveShare > 0.0f;
            var wheelLockedByHandbrake = !wheel.Steering && handbrake;
            if (drivePower > 0.0f && driveShare > 0.0f && !wheelLockedByHandbrake)
            {
                var speedLimiter = 1.0f - SmoothStep(Math.Clamp(MathF.Abs(Vector3.Dot(_rigidbody.Velocity, bodyForward)) / MathF.Max(maxSpeed, 1.0f), 0.0f, 1.0f));
                // `acceleration` calibrates peak tractive acceleration in first
                // gear. Wheel force in every other forward gear then follows
                // the transmission ratio, as it would for engine torque passed
                // through a gearbox: Fwheel = Tengine * ratio / wheelRadius.
                var gearForceRatio = MathF.Abs(GetGearRatio(_currentGear)) /
                                     MathF.Max(MathF.Abs(GetGearRatio(1)), 0.01f);
                var rawDrive = wheelForward * drivePower * acceleration * _rigidbody.Mass *
                               gearForceRatio * driveShare * speedLimiter;
                // Throttle against the service brake deliberately overwhelms
                // driven-wheel traction while the undriven axle holds the car.
                var burnoutTraction = burnout ? 0.45f : 1.0f;
                var wheelOverspeed = MathF.Max(
                    MathF.Abs(_drivenWheelSpinSpeed) - MathF.Abs(forwardSpeed), 0.0f);
                var longitudinalSlipRatio = wheelOverspeed / MathF.Max(MathF.Abs(forwardSpeed), 1.0f);
                var postPeakSlip = SmoothStep(InverseLerp(
                    MathF.Max(peakLongitudinalSlip, 0.0f),
                    MathF.Max(fullLongitudinalSlip, peakLongitudinalSlip + 0.01f),
                    longitudinalSlipRatio));
                var longitudinalGripRetention = Lerp(1.0f,
                    Math.Clamp(spinningTyreLongitudinalGrip, 0.0f, 1.0f), postPeakSlip);
                var lateralGripRetention = Lerp(1.0f,
                    Math.Clamp(spinningTyreLateralGrip, 0.0f, 1.0f), postPeakSlip);
                // driveShare divides the engine demand between driven wheels; it
                // must not also divide the contact patch's available traction.
                // Doing both made the traction cap so low that even the bottom
                // of the torque curve hit it, producing the same applied force
                // at practically every engine RPM.
                var tractionLimit = normalLoad * MathF.Max(wheelGrip, 0.0f) *
                                    MathF.Max(driveGrip, 0.0f) * burnoutTraction *
                                    longitudinalGripRetention;
                // A tyre using most of its friction budget longitudinally has
                // little cornering authority left. Base this loss on measured
                // wheel slip rather than requested engine force.
                tireForce = lateralForce * lateralGripRetention;
                var appliedDrive = ClampMagnitude(rawDrive, tractionLimit);
                tireForce += appliedDrive;

                // Torque which the contact patch cannot transmit accelerates
                // the wheel instead of vanishing. Since this state stores tread
                // speed (omega * radius), its acceleration is
                // excessForce * radius^2 / rotationalInertia.
                var excessDriveForce = MathF.Max(rawDrive.Length() - appliedDrive.Length(), 0.0f);
                var surfaceAcceleration = excessDriveForce * wheelRadius * wheelRadius /
                                          MathF.Max(drivenWheelInertia, 0.01f);
                drivenWheelSurfaceAcceleration = MathF.Max(drivenWheelSurfaceAcceleration, surfaceAcceleration);
            }

            // During a burnout the service brake holds the undriven wheels but
            // must not be interpreted as reverse throttle on the driven axle.
            if (reverseRequested)
            {
                if (driveShare > 0.0f && !wheelLockedByHandbrake)
                {
                    var reversePower = brake * GetEngineTorqueMultiplier();
                    var rawReverse = -wheelForward * reversePower * reverseAcceleration * _rigidbody.Mass * driveShare;
                    var tractionLimit = normalLoad * MathF.Max(wheelGrip, 0.0f) * MathF.Max(driveGrip, 0.0f);
                    var reverseDemand = rawReverse.Length() / MathF.Max(tractionLimit, 0.01f);
                    var poweredSlip = SmoothStep(InverseLerp(0.75f, 2.0f, reverseDemand));
                    tireForce = lateralForce * Lerp(1.0f, 0.18f, poweredSlip);
                    tireForce += ClampMagnitude(rawReverse, tractionLimit);
                }
            }
            else if (brake > 0.0f && !(burnout && drivenWheel))
            {
                // A fully locked tyre has no independent steering force. Its
                // available friction opposes the contact patch's actual planar
                // motion, preventing front-wheel steer from manufacturing yaw
                // under full braking and keeping force inside one grip budget.
                var planarVelocity = pointVelocity - bodyUp * Vector3.Dot(pointVelocity, bodyUp);
                var brakeLimit = normalLoad * MathF.Max(wheelGrip, 0.0f) * MathF.Max(brakeGrip, 0.0f);
                var lockedTireForce = Vector3.Zero;
                if (planarVelocity.LengthSquared() > 0.01f)
                {
                    var requestedBrake = brakePower * _rigidbody.Mass * 0.25f;
                    lockedTireForce = -Vector3.Normalize(planarVelocity) * MathF.Min(requestedBrake, brakeLimit);
                }
                tireForce = Vector3.Lerp(tireForce, lockedTireForce, Math.Clamp(brake, 0.0f, 1.0f));
            }

            if (handbrake && !wheel.Steering && MathF.Abs(forwardSpeed) > 0.1f)
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

        var drivenGroundSpeed = GetAverageDrivenWheelSpeed(driveSplit, chassisForwardSpeed);
        if (drivenWheelSurfaceAcceleration > 0.0f && drivePower > 0.0f)
        {
            var driveDirection = _currentGear < 0 ? -1.0f : 1.0f;
            _drivenWheelSpinSpeed += driveDirection * drivenWheelSurfaceAcceleration * deltaTime;
        }
        else
        {
            // Static friction couples a non-slipping driven wheel to the road.
            var coupling = 1.0f - MathF.Exp(-30.0f * MathF.Max(deltaTime, 0.0f));
            _drivenWheelSpinSpeed = Lerp(_drivenWheelSpinSpeed, drivenGroundSpeed, coupling);
        }

        if (diagnostics is not null)
        {
            Debug.Log(diagnostics.ToString());
        }
    }

    private void ApplyAerodynamicDownforce()
    {
        if (_rigidbody is null)
        {
            return;
        }

        var bodyUp = Up;
        var velocity = _rigidbody.Velocity;
        // Vertical velocity does not create useful wing loading. Project it out
        // so jumps and hard landings cannot manufacture extra downward force.
        var airflowAcrossCar = velocity - bodyUp * Vector3.Dot(velocity, bodyUp);
        var speedSquared = airflowAcrossCar.LengthSquared();
        var downforceMagnitude = 0.5f *
                                 MathF.Max(airDensity, 0.0f) *
                                 MathF.Max(downforceCoefficient, 0.0f) *
                                 MathF.Max(downforceArea, 0.0f) *
                                 speedSquared;
        if (downforceMagnitude <= 0.0001f)
        {
            return;
        }

        var frontBalance = Math.Clamp(frontDownforceBalance, 0.0f, 1.0f);
        var downforce = -bodyUp * downforceMagnitude;
        var bodyCenter = GameObject.WorldPosition;
        var frontCenter = GetAxleCenter(frontLeftWheelAnchor, frontRightWheelAnchor, bodyCenter);
        var rearCenter = GetAxleCenter(rearLeftWheelAnchor, rearRightWheelAnchor, bodyCenter);

        if (frontBalance > 0.0f)
        {
            _rigidbody.AddForceAtPosition(downforce * frontBalance, frontCenter);
        }
        if (frontBalance < 1.0f)
        {
            _rigidbody.AddForceAtPosition(downforce * (1.0f - frontBalance), rearCenter);
        }
    }

    private static Vector3 GetAxleCenter(GameObject? leftAnchor, GameObject? rightAnchor, Vector3 fallback)
    {
        if (leftAnchor is not null && rightAnchor is not null)
        {
            return (leftAnchor.WorldPosition + rightAnchor.WorldPosition) * 0.5f;
        }
        if (leftAnchor is not null)
        {
            return leftAnchor.WorldPosition;
        }
        return rightAnchor?.WorldPosition ?? fallback;
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

        var reverseRequested = brake > 0.0f && throttle <= 0.0f && forwardSpeed < 1.2f;
        if (reverseRequested)
        {
            _currentGear = -1;
        }
        else if (_currentGear <= 0 && (throttle > 0.0f || forwardSpeed >= -0.5f))
        {
            _currentGear = 1;
        }

        var drivenSpeed = GetAverageDrivenWheelSpeed(driveSplit, forwardSpeed);
        var rpmWheelSpeed = (throttle > 0.0f || (brake > 0.0f && _currentGear == -1)) &&
                            MathF.Abs(_drivenWheelSpinSpeed) > MathF.Abs(drivenSpeed)
            ? _drivenWheelSpinSpeed
            : drivenSpeed;
        var mechanicalRpm = EstimateRpmForGear(MathF.Abs(rpmWheelSpeed), _currentGear);
        var roadCoupledRpm = EstimateRpmForGear(MathF.Abs(drivenSpeed), _currentGear);
        if (_shiftTimer <= 0.0f && _currentGear > 0)
        {
            // Schedule shifts from road-coupled RPM, not spinning-wheel RPM.
            // Wheelspin may raise actual engine RPM to the limiter, but it is
            // not evidence that the car has reached the next gear's road speed.
            // Conversely, residual tyre slip must not block a shift once road
            // speed itself reaches the configured shift point.
            if (_currentGear < 6 &&
                roadCoupledRpm >= MathF.Max(upshiftRpm, idleRpm + 500.0f))
            {
                _currentGear++;
                _shiftTimer = MathF.Max(shiftDuration, 0.0f);
                mechanicalRpm = EstimateRpmForGear(MathF.Abs(drivenSpeed), _currentGear);
            }
            else if (_currentGear > 1 && roadCoupledRpm <= GetEffectiveDownshiftRpm(_currentGear))
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
        if (brake > 0.0f && _currentGear == -1)
        {
            targetRpm = MathF.Max(targetRpm, Lerp(MathF.Max(idleRpm, 0.0f), MathF.Max(launchRpm, idleRpm), brake * 0.65f));
        }

        targetRpm = Math.Clamp(targetRpm, MathF.Max(idleRpm, 0.0f), MathF.Max(redlineRpm, idleRpm + 100.0f));
        var response = _shiftTimer > 0.0f ? engineResponse * 1.8f : engineResponse;
        _engineRpm = Lerp(_engineRpm, targetRpm, 1.0f - MathF.Exp(-MathF.Max(response, 0.0f) * deltaTime));
        UpdateDrivenWheelSpinSpeed(deltaTime, throttle, brake, drivenSpeed);
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
        // RPM is clamped to redline, so the torque output must also reach zero
        // there. Otherwise the display stops while the car keeps accelerating
        // indefinitely on the remaining high-RPM torque.
        var limiterStart = MathF.Max(peak, redline * 0.97f);
        var revLimiter = 1.0f - SmoothStep(InverseLerp(limiterStart, redline, _engineRpm));
        return Math.Clamp(lowTorque * highRpmFade * revLimiter, 0.0f, 1.0f);
    }

    private void UpdateDrivenWheelSpinSpeed(float deltaTime, float throttle, float brake, float groundedDrivenSpeed)
    {
        var driveInput = throttle > 0.0f || (brake > 0.0f && _currentGear == -1);
        if (driveInput)
        {
            // ApplyWheels integrates excess engine torque after the contact
            // patch's transmitted force is known. Do not kinematically overwrite
            // that physical wheel-speed state here.
            if (MathF.Abs(_drivenWheelSpinSpeed) < MathF.Abs(groundedDrivenSpeed))
            {
                _drivenWheelSpinSpeed = groundedDrivenSpeed;
            }
            return;
        }

        _drivenWheelSpinSpeed = Lerp(_drivenWheelSpinSpeed, groundedDrivenSpeed,
            1.0f - MathF.Exp(-10.0f * deltaTime));
    }

    private float EstimateRpmForGear(float speed, int gear)
    {
        var rpm = MathF.Abs(WheelRpm(speed)) * MathF.Abs(GetGearRatio(gear)) * MathF.Max(finalDriveRatio, 0.01f);
        return Math.Clamp(rpm, MathF.Max(idleRpm, 0.0f), MathF.Max(redlineRpm, idleRpm + 100.0f));
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

    private float GetEffectiveDownshiftRpm(int gear)
    {
        var requestedRpm = MathF.Max(downshiftRpm, idleRpm);
        if (gear <= 1)
        {
            return requestedRpm;
        }

        // At the preceding gear's upshift road speed, this gear runs at the
        // upshift RPM scaled by the ratio step. Keep the downshift point below
        // that landing RPM so the two shift schedules cannot overlap and hunt.
        var previousRatio = MathF.Max(MathF.Abs(GetGearRatio(gear - 1)), 0.01f);
        var currentRatio = MathF.Abs(GetGearRatio(gear));
        var landingRpm = MathF.Max(upshiftRpm, idleRpm + 500.0f) * currentRatio / previousRatio;
        var nonOverlappingRpm = landingRpm - MathF.Max(shiftHysteresisRpm, 0.0f);
        return MathF.Max(idleRpm, MathF.Min(requestedRpm, nonOverlappingRpm));
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

    private void UpdateWheelVisuals(float deltaTime, DriveSplit driveSplit)
    {
        var travel = MathF.Max(suspensionTravel, 0.01f);

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

            // Physics compression changes at 60 Hz. Reject sub-millimetre solver
            // noise, then smooth only the rendered mesh. Neither value feeds
            // back into the spring calculation or its ray origin.
            var visualTarget = wheel.Compression;
            var normalizedDeadZone = MathF.Max(visualSuspensionDeadZone, 0.0f) / travel;
            if (MathF.Abs(visualTarget - wheel.VisualCompression) <= normalizedDeadZone)
            {
                visualTarget = wheel.VisualCompression;
            }
            var visualBlend = 1.0f - MathF.Exp(-MathF.Max(visualSuspensionSharpness, 0.1f) * MathF.Max(deltaTime, 0.0f));
            wheel.VisualCompression = Lerp(wheel.VisualCompression, visualTarget, visualBlend);
            // The anchor is the suspension's fully-compressed wheel position.
            // Move the mesh down by the current extension so it follows the
            // raycast contact instead of remaining attached to the raised body.
            var localRise = -(1.0f - wheel.VisualCompression) * travel;
            wheel.Visual.Position = new Vector3(
                wheel.BaseVisualPosition.X,
                wheel.BaseVisualPosition.Y + localRise,
                wheel.BaseVisualPosition.Z);

            var spinDirection = wheel.RightSide ? rightWheelSpinDirection : leftWheelSpinDirection;
            var driveWeight = wheel.Steering ? driveSplit.Front : driveSplit.Rear;
            var visualWheelSpeed = wheel.ForwardSpeed;
            // A locked rear wheel keeps translating with the chassis but has
            // zero angular speed. This must take priority over driven-wheel
            // wheelspin when throttle and handbrake are held together.
            if (!wheel.Steering && _handbrakeInput)
            {
                visualWheelSpeed = 0.0f;
            }
            else if (driveWeight > 0.0f && (_throttleInput > 0.0f || (_brakeInput > 0.0f && _currentGear == -1)) &&
                MathF.Abs(_drivenWheelSpinSpeed) > MathF.Abs(visualWheelSpeed))
            {
                visualWheelSpeed = _drivenWheelSpinSpeed;
            }
            wheel.SpinDegrees += visualWheelSpeed / MathF.Max(wheelRadius, 0.001f) * deltaTime * 180.0f / MathF.PI * spinDirection;

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
        public float VisualCompression;
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
