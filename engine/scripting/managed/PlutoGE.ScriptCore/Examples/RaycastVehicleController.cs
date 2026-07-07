using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Four-wheel raycast vehicle controller with spring suspension, tire grip, drive,
/// braking, handbrake, downforce, and optional visual wheel animation.
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

    [SerializedField] private float vehicleMass = 1200.0f;
    [SerializedField] private float wheelRadius = 0.36f;
    [SerializedField] private float suspensionTravel = 0.42f;
    [SerializedField] private float targetRideCompression = 0.35f;
    [SerializedField] private bool autoTuneSuspension = true;
    [SerializedField] private float dampingRatio = 0.65f;
    [SerializedField] private float springStrength = 48000.0f;
    [SerializedField] private float damperStrength = 6400.0f;
    [SerializedField] private float maxSuspensionForceMultiplier = 3.0f;
    [SerializedField] private float tireGrip = 1.35f;
    [SerializedField] private float longitudinalGripMultiplier = 2.35f;
    [SerializedField] private float minimumDriveNormalForce = 3200.0f;
    [SerializedField] private float lateralStiffness = 9500.0f;
    [SerializedField] private float longitudinalStiffness = 7200.0f;

    [SerializedField] private float motorForce = 36000.0f;
    [SerializedField] private float reverseForce = 6200.0f;
    [SerializedField] private float brakeForce = 12500.0f;
    [SerializedField] private float handbrakeForce = 9000.0f;
    [SerializedField] private float frontDriveBias = 0.0f;
    [SerializedField] private float chassisFriction = 0.05f;

    [SerializedField] private float maxSteerAngle = 32.0f;
    [SerializedField] private float steerResponse = 8.0f;
    [SerializedField] private float steerFadeSpeed = 38.0f;
    [SerializedField] private float handbrakeRearGripMultiplier = 0.42f;
    [SerializedField] private float driftSteerAssist = 5.5f;
    [SerializedField] private float driftFrontControlForce = 1.15f;
    [SerializedField] private float driftAssistSlipStart = 0.32f;
    [SerializedField] private float driftAssistSlipFull = 0.72f;
    [SerializedField] private float maxDriftYawRate = 2.6f;

    [SerializedField] private float downforce = 36.0f;
    [SerializedField] private float maxDownforceMultiplier = 2.0f;
    [SerializedField] private float rollingResistance = 18.0f;
    [SerializedField] private float coastLongitudinalDampingScale = 0.0015f;
    [SerializedField] private float brakingLongitudinalDampingScale = 0.008f;
    [SerializedField] private float uprightAssist = 7.5f;
    [SerializedField] private float yawAssist = 0.65f;
    [SerializedField] private float recoveryImpulse = 6500.0f;
    [SerializedField] private float maximumSpeed = 95.0f;
    [SerializedField] private float maximumPointSpeed = 80.0f;
    [SerializedField] private float minimumGroundNormalY = 0.35f;
    [SerializedField] private float suspensionRayStartOffset = 0.25f;
    [SerializedField] private float tireForceApplicationHeight = 0.35f;
    [SerializedField] private float forceSmoothingSharpness = 18.0f;
    [SerializedField] private float highSpeedGripFadeStart = 35.0f;
    [SerializedField] private float highSpeedGripFadeEnd = 85.0f;
    [SerializedField] private float highSpeedLateralGripMultiplier = 0.72f;
    [SerializedField] private float maxSuspensionJounceSpeed = 12.0f;
    [SerializedField] private float highSpeedPitchRollDamping = 3.0f;
    [SerializedField] private float groundedUpwardVelocityDamping = 2.0f;
    [SerializedField] private float speedLimiterStart = 0.82f;
    [SerializedField] private float overspeedDrag = 90.0f;

    [SerializedField] private bool steerFrontWheelAnchors = true;
    [SerializedField] private float physicsSteerDirection = -1.0f;
    [SerializedField] private Vector3 wheelSpinAxis = new Vector3(1.0f, 0.0f, 0.0f);
    [SerializedField] private Vector3 wheelSteerAxis = new Vector3(0.0f, 1.0f, 0.0f);
    [SerializedField] private float leftWheelSpinDirection = 1.0f;
    [SerializedField] private float rightWheelSpinDirection = 1.0f;

    [SerializedField] private bool keepBodyColliderAboveWheelContact = true;
    [SerializedField] private float bodyColliderGroundClearance = 0.08f;
    [SerializedField] private float antiScrapeForceMultiplier = 1.2f;

    [SerializedField] private bool lockCursorOnCreate = true;
    [SerializedField] private string groundTag = string.Empty;

    private readonly WheelState[] _wheels = new WheelState[4];
    private RigidbodyComponent? _rigidbody;
    private float _steer;
    private int _groundedWheelCount;

    public override void OnCreate()
    {
        _rigidbody = GameObject.GetComponent<RigidbodyComponent>();
        if (_rigidbody is not null)
        {
            _rigidbody.Mass = MathF.Max(vehicleMass, 50.0f);
            _rigidbody.IsKinematic = false;
            _rigidbody.UseGravity = true;
            _rigidbody.FreezeRotation = false;
            _rigidbody.LinearDrag = 0.02f;
            _rigidbody.AngularDrag = 0.8f;
            _rigidbody.Friction = MathF.Max(chassisFriction, 0.0f);
        }

        AutoTuneSuspensionIfNeeded();

        if (lockCursorOnCreate)
        {
            Input.CursorLocked = true;
        }

        ConfigureWheel(0, frontLeftWheelAnchor, frontLeftWheelVisual, true, false);
        ConfigureWheel(1, frontRightWheelAnchor, frontRightWheelVisual, true, true);
        ConfigureWheel(2, rearLeftWheelAnchor, rearLeftWheelVisual, false, false);
        ConfigureWheel(3, rearRightWheelAnchor, rearRightWheelVisual, false, true);
        AdjustBodyColliderClearance();
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

        ClampBodyVelocity();

        var speed = _rigidbody.Velocity.Length();
        var throttle = Input.IsKeyDown(KeyCode.W) ? 1.0f : 0.0f;
        var brakeInput = Input.IsKeyDown(KeyCode.S) ? 1.0f : 0.0f;
        var handbrake = Input.IsKeyDown(KeyCode.Space);
        var steerInput = 0.0f;
        if (Input.IsKeyDown(KeyCode.A)) steerInput += 1.0f;
        if (Input.IsKeyDown(KeyCode.D)) steerInput -= 1.0f;

        var steerSpeed = ForwardSpeedAbs();
        var steerFade = 1.0f / (1.0f + steerSpeed / MathF.Max(steerFadeSpeed, 0.01f));
        var targetSteer = steerInput * maxSteerAngle * steerFade;
        _steer = Lerp(_steer, targetSteer, 1.0f - MathF.Exp(-steerResponse * deltaTime));

        if (Input.IsKeyPressed(KeyCode.R))
        {
            RecoverCar();
        }

        ApplyVehicleForces(deltaTime, throttle, brakeInput, handbrake, steerInput);
        ApplyAeroAndAssists(speed, steerInput, deltaTime);
        ApplyAntiScrapeLift();
        UpdateWheelVisuals(deltaTime);
    }

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

    private void AutoTuneSuspensionIfNeeded()
    {
        if (!autoTuneSuspension || _rigidbody is null)
        {
            return;
        }

        var targetCompressionDistance = MathF.Max(suspensionTravel * Math.Clamp(targetRideCompression, 0.05f, 0.95f), 0.001f);
        var sprungMassPerWheel = _rigidbody.Mass * 0.25f;
        springStrength = _rigidbody.Mass * 9.81f / (4.0f * targetCompressionDistance);
        damperStrength = 2.0f * MathF.Sqrt(springStrength * sprungMassPerWheel) * Math.Clamp(dampingRatio, 0.1f, 1.5f);
    }

    private void AdjustBodyColliderClearance()
    {
        if (!keepBodyColliderAboveWheelContact)
        {
            return;
        }

        var collider = GameObject.GetComponent<ColliderComponent>();
        if (collider is null || collider.Shape != ColliderShape.Box)
        {
            return;
        }

        var hasWheel = false;
        var lowestWheelMount = float.MaxValue;
        foreach (var wheel in _wheels)
        {
            if (wheel.Anchor is null)
            {
                continue;
            }

            hasWheel = true;
            lowestWheelMount = MathF.Min(lowestWheelMount, wheel.Anchor.Position.Y);
        }

        if (!hasWheel)
        {
            return;
        }

        var size = collider.Size;
        var center = collider.Center;
        var targetCompressionDistance = suspensionTravel * Math.Clamp(targetRideCompression, 0.0f, 1.0f);
        var fullCompressionGroundY = lowestWheelMount + suspensionTravel - targetCompressionDistance - wheelRadius;
        var minimumBodyBottom = fullCompressionGroundY + MathF.Max(bodyColliderGroundClearance, 0.0f);
        var currentBodyBottom = center.Y - size.Y * 0.5f;
        if (currentBodyBottom < minimumBodyBottom)
        {
            center.Y += minimumBodyBottom - currentBodyBottom;
            collider.Center = center;
        }
    }

    private void ApplyVehicleForces(float deltaTime, float throttle, float brakeInput, bool handbrake, float steerInput)
    {
        var frontDrive = Math.Clamp(frontDriveBias, 0.0f, 1.0f);
        var rearDrive = 1.0f - frontDrive;
        var forwardSpeedAbs = ForwardSpeedAbs();
        var limiterStart = MathF.Max(maximumSpeed * Math.Clamp(speedLimiterStart, 0.1f, 1.0f), 0.1f);
        var speedLimiter = 1.0f - SmoothStep(InverseLerp(limiterStart, MathF.Max(maximumSpeed, limiterStart + 0.1f), forwardSpeedAbs));
        _groundedWheelCount = 0;

        for (var index = 0; index < _wheels.Length; ++index)
        {
            var wheel = _wheels[index];
            if (wheel.Anchor is null)
            {
                continue;
            }

            var anchorPosition = wheel.Anchor.WorldPosition;
            var forward = SafeNormalize(GameObject.Forward, -Vector3.UnitZ);
            var right = SafeNormalize(GameObject.Right, Vector3.UnitX);
            if (wheel.Steering)
            {
                var physicsSteer = _steer * MathF.Sign(NonZero(physicsSteerDirection));
                forward = RotateAroundUp(forward, physicsSteer);
                right = RotateAroundUp(right, physicsSteer);
            }

            var up = SafeNormalize(Vector3.Cross(right, forward), Vector3.UnitY);
            var down = -up;
            var rayStartOffset = MathF.Max(suspensionRayStartOffset, 0.0f);
            var rayOrigin = anchorPosition + up * rayStartOffset;
            var targetCompressionDistance = suspensionTravel * Math.Clamp(targetRideCompression, 0.0f, 1.0f);
            var maxContactDistanceFromAnchor = wheelRadius + suspensionTravel - targetCompressionDistance;
            var rayLength = maxContactDistanceFromAnchor + rayStartOffset;
            RaycastHit hit;
            var hitGround = string.IsNullOrWhiteSpace(groundTag)
                ? Physics.Raycast(rayOrigin, down, rayLength, GameObject, out hit)
                : Physics.RaycastTagged(rayOrigin, down, rayLength, groundTag, GameObject, out hit);

            if (!hitGround || Vector3.Dot(hit.Normal, up) < minimumGroundNormalY)
            {
                wheel.Grounded = false;
                wheel.Compression = 0.0f;
                wheel.SmoothedSuspensionForce = Vector3.Lerp(wheel.SmoothedSuspensionForce, Vector3.Zero, ForceSmoothingAmount(deltaTime));
                wheel.SmoothedTireForce = Vector3.Lerp(wheel.SmoothedTireForce, Vector3.Zero, ForceSmoothingAmount(deltaTime));
                _wheels[index] = wheel;
                continue;
            }

            var pointVelocity = EstimatePointVelocity(anchorPosition);
            _groundedWheelCount++;
            var distanceFromAnchor = MathF.Max(hit.Distance - rayStartOffset, 0.0f);
            var compression = Math.Clamp(targetCompressionDistance + wheelRadius - distanceFromAnchor, 0.0f, suspensionTravel);
            var compression01 = compression / MathF.Max(suspensionTravel, 0.0001f);
            var springVelocity = Math.Clamp(Vector3.Dot(pointVelocity, up), -maxSuspensionJounceSpeed, maxSuspensionJounceSpeed);
            var maxSuspensionForce = _rigidbody!.Mass * 9.81f * MathF.Max(maxSuspensionForceMultiplier, 0.5f) * 0.25f;
            var normalForceMagnitude = Math.Clamp(
                compression * springStrength - springVelocity * damperStrength,
                0.0f,
                maxSuspensionForce);
            var suspensionPoint = hit.Point + up * wheelRadius;
            var tireForcePoint = hit.Point + up * MathF.Max(tireForceApplicationHeight, 0.0f);
            var smoothingAmount = ForceSmoothingAmount(deltaTime);

            var suspensionForce = up * normalForceMagnitude;
            wheel.SmoothedSuspensionForce = Vector3.Lerp(wheel.SmoothedSuspensionForce, suspensionForce, smoothingAmount);
            _rigidbody.AddForceAtPosition(wheel.SmoothedSuspensionForce, suspensionPoint);

            var lateralSpeed = Vector3.Dot(pointVelocity, right);
            var forwardSpeed = Vector3.Dot(pointVelocity, forward);
            var speed01 = InverseLerp(highSpeedGripFadeStart, highSpeedGripFadeEnd, _rigidbody.Velocity.Length());
            var highSpeedGrip = Lerp(1.0f, highSpeedLateralGripMultiplier, speed01);
            var gripMultiplier = (!wheel.Steering && handbrake) ? handbrakeRearGripMultiplier : 1.0f;
            var lateralForce = -right * lateralSpeed * lateralStiffness * gripMultiplier;
            var maxLateralForce = normalForceMagnitude * tireGrip * gripMultiplier * highSpeedGrip;
            lateralForce = ClampMagnitude(lateralForce, maxLateralForce);

            var driveShare = wheel.Steering ? frontDrive * 0.5f : rearDrive * 0.5f;
            var driveForce = throttle * motorForce * driveShare * speedLimiter;
            if (brakeInput > 0.0f)
            {
                driveForce += forwardSpeed > 1.0f
                    ? -MathF.Sign(forwardSpeed) * brakeForce * brakeInput * 0.25f
                    : -reverseForce * brakeInput * driveShare;
            }

            var brake = brakeInput > 0.0f && forwardSpeed > 1.0f
                ? -forward * MathF.Sign(forwardSpeed) * brakeForce * brakeInput * 0.25f
                : Vector3.Zero;
            var handbrakeForceVector = !wheel.Steering && handbrake
                ? -forward * MathF.Sign(NonZero(forwardSpeed)) * handbrakeForce * 0.5f
                : Vector3.Zero;
            var longitudinalDampingScale = brakeInput > 0.0f || handbrake
                ? brakingLongitudinalDampingScale
                : coastLongitudinalDampingScale;
            var longitudinalDamping = -forward * forwardSpeed * longitudinalStiffness * MathF.Max(longitudinalDampingScale, 0.0f);
            var rolling = -forward * forwardSpeed * rollingResistance;

            var longitudinalForce = forward * driveForce + brake + handbrakeForceVector + longitudinalDamping + rolling;
            var driveNormalForce = MathF.Max(normalForceMagnitude, minimumDriveNormalForce);
            var maxLongitudinalForce = driveNormalForce * tireGrip * longitudinalGripMultiplier * gripMultiplier;
            longitudinalForce = ClampMagnitude(longitudinalForce, maxLongitudinalForce);

            var driftControlForce = Vector3.Zero;
            if (wheel.Steering && MathF.Abs(steerInput) > 0.01f)
            {
                var bodyRight = SafeNormalize(GameObject.Right, Vector3.UnitX);
                var bodyForward = SafeNormalize(GameObject.Forward, -Vector3.UnitZ);
                var bodyForwardSpeed = MathF.Abs(Vector3.Dot(_rigidbody.Velocity, bodyForward));
                var bodyLateralSpeed = MathF.Abs(Vector3.Dot(_rigidbody.Velocity, bodyRight));
                var slipAmount = PlanarSlipAmount(bodyForwardSpeed, bodyLateralSpeed);
                var assistAmount = SmoothStep(InverseLerp(driftAssistSlipStart, driftAssistSlipFull, slipAmount));
                var steerDirection = steerInput * MathF.Sign(NonZero(physicsSteerDirection));
                driftControlForce = bodyRight * steerDirection * normalForceMagnitude * MathF.Max(driftFrontControlForce, 0.0f) * assistAmount;
            }

            var tireForce = lateralForce + longitudinalForce + driftControlForce;
            wheel.SmoothedTireForce = Vector3.Lerp(wheel.SmoothedTireForce, tireForce, smoothingAmount);
            _rigidbody.AddForceAtPosition(wheel.SmoothedTireForce, tireForcePoint);

            wheel.Grounded = true;
            wheel.Compression = compression01;
            wheel.ForwardSpeed = forwardSpeed;
            wheel.VisualSteerAngle = wheel.Steering ? _steer : 0.0f;
            _wheels[index] = wheel;
        }
    }

    private Vector3 EstimatePointVelocity(Vector3 worldPoint)
    {
        if (_rigidbody is null)
        {
            return Vector3.Zero;
        }

        var radius = worldPoint - GameObject.WorldPosition;
        var pointVelocity = _rigidbody.Velocity + Vector3.Cross(_rigidbody.AngularVelocity, radius);
        return ClampMagnitude(pointVelocity, MathF.Max(maximumPointSpeed, 5.0f));
    }

    private float ForwardSpeedAbs()
    {
        if (_rigidbody is null)
        {
            return 0.0f;
        }

        var forwardAxis = SafeNormalize(GameObject.Forward, -Vector3.UnitZ);
        return MathF.Abs(Vector3.Dot(_rigidbody.Velocity, forwardAxis));
    }

    private void ClampBodyVelocity()
    {
        if (_rigidbody is null)
        {
            return;
        }

        var failSafeSpeed = MathF.Max(maximumSpeed, 1.0f) * 1.45f;
        if (_rigidbody.Velocity.LengthSquared() > failSafeSpeed * failSafeSpeed)
        {
            _rigidbody.Velocity = Vector3.Normalize(_rigidbody.Velocity) * failSafeSpeed;
        }

        const float maxAngularSpeed = 16.0f;
        if (_rigidbody.AngularVelocity.LengthSquared() > maxAngularSpeed * maxAngularSpeed)
        {
            _rigidbody.AngularVelocity = Vector3.Normalize(_rigidbody.AngularVelocity) * maxAngularSpeed;
        }
    }

    private void ApplyAeroAndAssists(float speed, float steerInput, float deltaTime)
    {
        if (_rigidbody is null)
        {
            return;
        }

        var velocity = _rigidbody.Velocity;
        if (velocity.LengthSquared() > 0.01f)
        {
            var maxDownforce = _rigidbody.Mass * 9.81f * MathF.Max(maxDownforceMultiplier, 0.0f);
            var downforceMagnitude = MathF.Min(downforce * speed * speed, maxDownforce);
            _rigidbody.AddForce(Vector3.UnitY * -downforceMagnitude);

            var maxSpeed = MathF.Max(maximumSpeed, 1.0f);
            if (speed > maxSpeed)
            {
                var overspeed = speed - maxSpeed;
                _rigidbody.AddForce(-Vector3.Normalize(velocity) * overspeed * overspeed * MathF.Max(overspeedDrag, 0.0f));
            }
        }

        var right = SafeNormalize(GameObject.Right, Vector3.UnitX);
        var forward = SafeNormalize(GameObject.Forward, -Vector3.UnitZ);
        var up = SafeNormalize(Vector3.Cross(right, forward), Vector3.UnitY);
        var uprightCorrection = Vector3.Cross(up, Vector3.UnitY) * uprightAssist;
        var yawDamping = -Vector3.UnitY * _rigidbody.AngularVelocity.Y * yawAssist;
        _rigidbody.AngularVelocity += (uprightCorrection + yawDamping) * deltaTime;
        ApplyDriftSteerAssist(steerInput, deltaTime);

        var highSpeedAmount = InverseLerp(highSpeedGripFadeStart, highSpeedGripFadeEnd, speed);
        if (highSpeedAmount > 0.0f)
        {
            var pitchRollDamping = MathF.Exp(-highSpeedPitchRollDamping * highSpeedAmount * deltaTime);
            var angularVelocity = _rigidbody.AngularVelocity;
            _rigidbody.AngularVelocity = new Vector3(
                angularVelocity.X * pitchRollDamping,
                angularVelocity.Y,
                angularVelocity.Z * pitchRollDamping);
        }

        if (_groundedWheelCount > 0 && _rigidbody.Velocity.Y > 0.0f)
        {
            var bodyVelocity = _rigidbody.Velocity;
            var verticalDamping = MathF.Exp(-groundedUpwardVelocityDamping * deltaTime);
            _rigidbody.Velocity = new Vector3(bodyVelocity.X, bodyVelocity.Y * verticalDamping, bodyVelocity.Z);
        }
    }

    private void ApplyDriftSteerAssist(float steerInput, float deltaTime)
    {
        if (_rigidbody is null || _groundedWheelCount <= 0 || MathF.Abs(steerInput) <= 0.01f)
        {
            return;
        }

        var forward = SafeNormalize(GameObject.Forward, -Vector3.UnitZ);
        var right = SafeNormalize(GameObject.Right, Vector3.UnitX);
        var velocity = _rigidbody.Velocity;
        var forwardSpeed = MathF.Abs(Vector3.Dot(velocity, forward));
        var lateralSpeed = MathF.Abs(Vector3.Dot(velocity, right));
        var planarSpeed = forwardSpeed + lateralSpeed;
        if (planarSpeed <= 0.1f)
        {
            return;
        }

        var slipAmount = PlanarSlipAmount(forwardSpeed, lateralSpeed);
        var assistAmount = SmoothStep(InverseLerp(driftAssistSlipStart, driftAssistSlipFull, slipAmount));
        if (assistAmount <= 0.0f)
        {
            return;
        }

        var physicsSteer = steerInput * MathF.Sign(NonZero(physicsSteerDirection));
        var targetYawRate = physicsSteer * MathF.Max(maxDriftYawRate, 0.0f);
        var angularVelocity = _rigidbody.AngularVelocity;
        var yawBlend = 1.0f - MathF.Exp(-MathF.Max(driftSteerAssist, 0.0f) * assistAmount * deltaTime);
        angularVelocity.Y = Lerp(angularVelocity.Y, targetYawRate, yawBlend);
        _rigidbody.AngularVelocity = angularVelocity;
    }

    private static float PlanarSlipAmount(float forwardSpeed, float lateralSpeed)
    {
        var planarSpeed = MathF.Abs(forwardSpeed) + MathF.Abs(lateralSpeed);
        return planarSpeed <= 0.1f ? 0.0f : MathF.Abs(lateralSpeed) / planarSpeed;
    }

    private void ApplyAntiScrapeLift()
    {
        if (_rigidbody is null || _groundedWheelCount <= 0)
        {
            return;
        }

        var collider = GameObject.GetComponent<ColliderComponent>();
        if (collider is null || collider.Shape != ColliderShape.Box)
        {
            return;
        }

        var bodyBottomWorldY = GameObject.WorldPosition.Y + collider.Center.Y - collider.Size.Y * 0.5f;
        var groundY = float.MinValue;
        var targetCompressionDistance = suspensionTravel * Math.Clamp(targetRideCompression, 0.0f, 1.0f);
        foreach (var wheel in _wheels)
        {
            if (!wheel.Grounded || wheel.Anchor is null)
            {
                continue;
            }

            groundY = MathF.Max(groundY, wheel.Anchor.WorldPosition.Y + wheel.Compression * suspensionTravel - targetCompressionDistance - wheelRadius);
        }

        if (groundY == float.MinValue)
        {
            return;
        }

        var clearance = bodyBottomWorldY - groundY;
        var targetClearance = MathF.Max(bodyColliderGroundClearance, 0.0f);
        if (clearance >= targetClearance)
        {
            return;
        }

        var lift01 = Math.Clamp((targetClearance - clearance) / MathF.Max(targetClearance, 0.001f), 0.0f, 1.0f);
        _rigidbody.AddForce(Vector3.UnitY * _rigidbody.Mass * 9.81f * MathF.Max(antiScrapeForceMultiplier, 0.0f) * lift01);
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
        _rigidbody.AddImpulse(Vector3.UnitY * recoveryImpulse);
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

            var targetCompressionDistance = suspensionTravel * Math.Clamp(targetRideCompression, 0.0f, 1.0f);
            var localRise = wheel.Compression * suspensionTravel - targetCompressionDistance;
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

        var lengthSquared = value.LengthSquared();
        if (lengthSquared <= maxLength * maxLength)
        {
            return value;
        }

        return Vector3.Normalize(value) * maxLength;
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

    private float ForceSmoothingAmount(float deltaTime)
    {
        return 1.0f - MathF.Exp(-MathF.Max(forceSmoothingSharpness, 0.0f) * deltaTime);
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
        public Vector3 SmoothedSuspensionForce;
        public Vector3 SmoothedTireForce;
        public Vector3 BaseAnchorRotation;
        public Vector3 BaseVisualPosition;
        public Vector3 BaseVisualRotation;
    }
}
