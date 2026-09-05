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
    [SerializedField] private float boostCapacity = 100.0f;
    [SerializedField] private float boostConsumption = 25.0f;
    public float BoostAmount { get; private set; } = 100.0f;
    public float BoostFraction => BoostAmount / MathF.Max(boostCapacity, 1.0f);
    public bool IsBoosting { get; private set; }
    public bool IsFrozen => _body is null || _body.IsKinematic;

    public bool CollectBoost(float amount)
    {
        if (IsFrozen || BoostAmount >= MathF.Max(boostCapacity, 1.0f)) return false;
        BoostAmount = Math.Clamp(BoostAmount + MathF.Max(amount, 0.0f), 0.0f, MathF.Max(boostCapacity, 1.0f));
        return true;
    }

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
    [SerializedField] private float maximumSteerAngle = 32.0f;
    [SerializedField] private float steeringResponse = 12.0f;
    [SerializedField] private float powerslideFrontGrip = 0.45f;
    [SerializedField] private float powerslideRearGrip = 0.08f;
    [SerializedField] private float flipWindow = 1.5f;
    [SerializedField] private float flipImpulse = 9.0f;
    [SerializedField] private float flipDuration = 0.7f;
    [SerializedField] private float flipDeadZone = 0.2f;
    public bool FlipAvailable => _airJumpAvailable && _flipTimeRemaining > 0.0f;
    public float FlipTimeRemaining => FlipAvailable ? _flipTimeRemaining : 0.0f;
    public bool IsFlipping => _flipElapsed >= 0.0f;
    public bool IsPowersliding => Grounded && _powerslide;
    public int ResetWheelContacts { get; private set; }
    // Distances are relative to chassis height, so resized sample cars keep
    // their wheel clearance. Springs provide support, the body handles impacts.
    [SerializedField] private float suspensionRestLength = 0.85f;
    [SerializedField] private float suspensionTravel = 0.55f;
    [SerializedField] private float suspensionStiffness = 180.0f;
    [SerializedField] private float suspensionDamping = 18.0f;
    [SerializedField] private float surfaceAlignment = 60.0f;
    [SerializedField] private float surfaceDamping = 8.0f;
    [SerializedField] private float wallAdhesion = 12.0f;
    public bool Grounded { get; private set; }
    public Vector3 SurfaceNormal { get; private set; } = Vector3.UnitY;
    [SerializedField] private float airControlForce  = 3000.0f;
    [SerializedField] private float controlLeverArm  = 1.35000002f;
    [SerializedField] private float jumpImpulse  = 7.0f;
    [SerializedField] private float jumpBufferDuration = 0.16f;
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
    private bool _powerslide;
    private float _steerAngle;
    private Vector2 _queuedJumpDirection;
    private bool _airJumpAvailable = true;
    private float _flipTimeRemaining = 1.5f;
    private float _flipElapsed = -1.0f;
    private float _flipAngle;
    private Vector3 _flipAxis;
    private bool _boosting;
    private bool _jumpHeld;
    private bool _jumpQueued;
    private bool _jumpAvailable;
    private float _jumpBufferTimer;
    private float _groundedGraceTimer;
    private float _jumpDetachTimer;
    private readonly RaycastHit[] _wheelHits = new RaycastHit[4];
    private readonly Vector3[] _wheelMounts = new Vector3[4];
    private readonly bool[] _wheelTouching = new bool[4];
    private readonly GameObject?[] _wheelVisuals = new GameObject?[4];
    private int _wheelCount;
    private Vector3 _previousSurfaceNormal = Vector3.UnitY;
    private bool _hadSurface;
    private Vector3 _surfaceAngularVelocity;
    private InputActionMap? _inputActions;
    private string _actionPrefix = "P1";

    public override void OnCreate()
    {
        _actionPrefix = $"P{Math.Max(playerNumber, 1)}";
        BoostAmount = MathF.Max(boostCapacity, 1.0f);
        _flipTimeRemaining = MathF.Max(flipWindow, 0.0f);
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
        // Tyre grip is applied only at suspension contacts. Chassis friction
        // must stay low so the nose and belly slide across mesh seams.
        _body.Friction = 0.05f;
        var collider = GameObject.GetComponent<ColliderComponent>();
        if (collider is not null)
        {
            collider.Shape = ColliderShape.Box;
            collider.Center = new Vector3(0.0f, 0.10f, 0.0f);
            collider.Size = new Vector3(0.92f, 0.75f, 0.88f);
        }
        _body.UseGravity = true;
        _body.IsKinematic = false;
        _body.FreezeRotation = false;
        for (var wheel = 0; wheel < 4; wheel++)
            _wheelVisuals[wheel] = GameObject.Find($"{GameObject.Name} Wheel {wheel + 1}");
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
        _powerslide = IsDown("Powerslide");
        _boosting = IsDown("Boost");
        // Keep the edge locally instead of relying only on the engine's
        // one-render-frame pressed flag. Fixed updates can run before or after
        // that flag is sampled, while the held state remains stable.
        var jumpDown = IsDown("Jump");
        if ((jumpDown && !_jumpHeld) || WasPressed("Jump"))
        {
            _queuedJumpDirection = new Vector2(_steering, -_pitch);
            _jumpQueued = true;
            _jumpBufferTimer = MathF.Max(jumpBufferDuration, 0.0f);
        }
        _jumpHeld = jumpDown;

        // Wheel meshes follow suspension travel without adding snagging wheel
        // colliders. The chassis remains the sole body for ball/car impacts.
        var height = MathF.Max(MathF.Abs(GameObject.Scale.Y), 0.2f);
        for (var wheel = 0; wheel < 4; wheel++)
        {
            var visual = _wheelVisuals[wheel];
            if (visual is null) continue;
            var distance = _wheelTouching[wheel] ? _wheelHits[wheel].Distance / height : suspensionRestLength + suspensionTravel;
            var position = visual.Position;
            position.Y = 0.4f - Math.Clamp(distance, 0.35f, suspensionRestLength + suspensionTravel);
            visual.Position = position;
            visual.RotationQuaternion = Quaternion.CreateFromAxisAngle(Vector3.UnitY, wheel < 2 ? -_steerAngle : 0.0f) *
                                        Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.PI * 0.5f);
        }

        if (GameObject.WorldPosition.Y < respawnHeight)
        {
            ResetCar();
        }
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        IsBoosting = false;
        if (fixedDeltaTime <= 0.0f) return;
        if (_body is null || _body.IsKinematic)
        {
            Grounded = false;
            _hadSurface = false;
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
        _jumpDetachTimer = MathF.Max(0.0f, _jumpDetachTimer - fixedDeltaTime);
        QueryWheels(up, right, forward, fixedDeltaTime);
        var grounded = Grounded;

        // Recharge only from three close wheel contacts, never a belly hit or
        // long suspension probe. Ball contacts count without becoming tyre grip.
        if (ResetWheelContacts >= 3)
        {
            _jumpAvailable = true;
            _airJumpAvailable = true;
            _flipTimeRemaining = MathF.Max(flipWindow, 0.0f);
            _flipElapsed = -1.0f;
        }
        else
        {
            _flipTimeRemaining = MathF.Max(0.0f, _flipTimeRemaining - fixedDeltaTime);
        }
        if (grounded) _groundedGraceTimer = 0.12f;
        else
        {
            _groundedGraceTimer = MathF.Max(0.0f, _groundedGraceTimer - fixedDeltaTime);
            if (_groundedGraceTimer <= 0.0f) _jumpAvailable = false;
        }

        if (_jumpQueued && _jumpAvailable && (grounded || _groundedGraceTimer > 0.0f))
        {
            // Jump away from the contacted surface, including walls. The
            // detachment timer prevents springs from recapturing that jump.
            _body.AddImpulse(Vector3.Zero);
            var jumpNormal = grounded ? SurfaceNormal : up;
            _body.AddImpulse(jumpNormal * jumpImpulse * _body.Mass);
            // AddImpulse is applied to the native body immediately, while the
            // managed velocity property is synchronized after this callback.
            // Keep our local velocity in step with the known impulse so the
            // speed cap below cannot restore the pre-jump velocity.
            velocity += jumpNormal * jumpImpulse;
            _flipTimeRemaining = MathF.Max(flipWindow, 0.0f);
            _jumpQueued = false;
            _jumpAvailable = false;
            _jumpBufferTimer = 0.0f;
            _groundedGraceTimer = 0.0f;
            grounded = false;
            Grounded = false;
            _hadSurface = false;
            _jumpDetachTimer = 0.20f;
        }
        else if (_jumpQueued && !grounded && FlipAvailable && !IsFlipping)
        {
            var direction = _queuedJumpDirection;
            var impulse = up * jumpImpulse;
            if (direction.LengthSquared() >= flipDeadZone * flipDeadZone)
            {
                direction = Vector2.Normalize(direction);
                var dodgeDirection = SafeDirection(forward * direction.Y + right * direction.X, forward);
                impulse = dodgeDirection * flipImpulse;
                _flipAxis = SafeDirection(Vector3.Cross(up, dodgeDirection), right);
                _flipAngle = 0.0f;
                _flipElapsed = 0.0f;
            }
            _body.AddImpulse(impulse * _body.Mass);
            velocity += impulse;
            _airJumpAvailable = false;
            _jumpAvailable = false;
            _jumpQueued = false;
            _jumpBufferTimer = 0.0f;
            _groundedGraceTimer = 0.0f;
            _jumpDetachTimer = 0.20f;
        }

        if (IsFlipping) grounded = Grounded = false;
        var steerLimit = maximumSteerAngle * MathF.PI / 180.0f *
            (1.0f - 0.55f * Math.Clamp(velocity.Length() / MathF.Max(maximumBoostSpeed, 1.0f), 0.0f, 1.0f));
        _steerAngle += (_steering * steerLimit - _steerAngle) * (1.0f - MathF.Exp(-steeringResponse * fixedDeltaTime));

        if (grounded)
        {
            ApplySuspension(forward, velocity);
            var normal = SurfaceNormal;
            ApplyTyres(forward, right);
            var yaw = Vector3.Dot(_body.AngularVelocity, normal);
            var tiltVelocity = _body.AngularVelocity - normal * yaw;
            var alignmentAcceleration = Vector3.Cross(up, normal) * surfaceAlignment +
                (_surfaceAngularVelocity - tiltVelocity) * surfaceDamping;
            // Surface alignment supplies pitch/roll only. Yaw comes from the
            // steered front tyres and their force at the wheel mounts.
            ApplyAngularAcceleration(alignmentAcceleration, right, up, forward);

        }
        else if (IsFlipping)
        {
            // A torque-driven 360-degree dodge. The eased angular target brakes
            // near completion; gravity, boost and collisions remain physical.
            var duration = MathF.Max(flipDuration, 0.2f);
            _flipAngle += Vector3.Dot(_body.AngularVelocity, _flipAxis) * fixedDeltaTime;
            _flipElapsed += fixedDeltaTime;
            var t = MathF.Min(_flipElapsed / duration, 1.0f);
            var targetAngle = 2.0f * MathF.PI * t * t * (3.0f - 2.0f * t);
            var targetSpeed = 12.0f * MathF.PI * t * (1.0f - t) / duration;
            var spin = Vector3.Dot(_body.AngularVelocity, _flipAxis);
            var angularAcceleration = _flipAxis * ((targetAngle - _flipAngle) * 120.0f + (targetSpeed - spin) * 22.0f) -
                                      (_body.AngularVelocity - _flipAxis * spin) * 10.0f;
            ApplyAngularAcceleration(angularAcceleration, right, up, forward);
            // Briefly settle the angular target so the dodge finishes upright
            // rather than coasting through an extra quarter turn.
            if (_flipElapsed >= duration + 0.15f) _flipElapsed = -1.0f;
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
        if (_boosting && BoostAmount > 0.0f && fixedDeltaTime > 0.0f)
        {
            var requested = MathF.Max(boostConsumption, 0.01f) * fixedDeltaTime;
            var consumed = MathF.Min(BoostAmount, requested);
            BoostAmount = MathF.Max(0.0f, BoostAmount - consumed);
            IsBoosting = true;
            if (boostForwardSpeed < maximumBoostSpeed)
                _body.AddForce(forward * boostAcceleration * _body.Mass * (consumed / requested));
        }

        // Apply frame-rate-independent drag directly against angular momentum.
        // This changes rotational velocity, not orientation, so impacts and air
        // controls still produce genuine momentum that then decays over time.
        var angularDamping = grounded || IsFlipping ? 1.0f : MathF.Exp(-MathF.Max(angularMomentumDrag, 0.0f) * fixedDeltaTime);
        _body.AngularVelocity *= angularDamping;

        // Forces and collisions still determine both momentum vectors. Only
        // their magnitudes are capped, preserving combined pitch/yaw/roll.
        _body.Velocity = ClampMagnitude(velocity, MathF.Max(maximumBoostSpeed, 0.0f));
        _body.AngularVelocity = ClampMagnitude(_body.AngularVelocity, IsFlipping
            ? MathF.Max(maximumAngularSpeed, 4.0f * MathF.PI / MathF.Max(flipDuration, 0.2f))
            : MathF.Max(maximumAngularSpeed, 0.0f));

    }

    public void ResetCar()
    {
        ResetCar(_spawnPosition, _spawnRotation);
    }

    public void ResetCar(Vector3 worldPosition, Vector3 worldRotation)
    {
        BoostAmount = MathF.Max(boostCapacity, 1.0f);
        IsBoosting = false;
        _boosting = false;
        GameObject.WorldPosition = worldPosition;
        GameObject.WorldRotation = worldRotation;
        _jumpQueued = false;
        _jumpHeld = false;
        _jumpAvailable = false;
        _airJumpAvailable = true;
        _flipTimeRemaining = MathF.Max(flipWindow, 0.0f);
        _flipElapsed = -1.0f;
        _steerAngle = 0.0f;
        ResetWheelContacts = 0;
        _jumpBufferTimer = 0.0f;
        _groundedGraceTimer = 0.0f;
        _jumpDetachTimer = 0.0f;
        Grounded = false;
        _hadSurface = false;
        if (_body is not null)
        {
            _body.Velocity = Vector3.Zero;
            _body.AngularVelocity = Vector3.Zero;
        }
    }

    public void SetFrozen(bool frozen)
    {
        IsBoosting = false;
        if (_body is null) return;
        _body.Velocity = Vector3.Zero;
        _body.AngularVelocity = Vector3.Zero;
        _body.IsKinematic = frozen;
    }

    private void QueryWheels(Vector3 up, Vector3 right, Vector3 forward, float dt)
    {
        var scale = Vector3.Abs(GameObject.Scale);
        var chassisHeight = MathF.Max(scale.Y, 0.2f);
        var rayLength = chassisHeight * (suspensionRestLength + suspensionTravel);
        var normalSum = Vector3.Zero;
        _wheelCount = 0;
        ResetWheelContacts = 0;
        for (var wheel = 0; wheel < 4; wheel++)
        {
            var mount = GameObject.WorldPosition + right * (scale.X * 0.42f * ((wheel & 1) == 0 ? -1 : 1)) +
                        forward * (scale.Z * 0.36f * (wheel < 2 ? 1 : -1));
            _wheelMounts[wheel] = mount;
            _wheelTouching[wheel] = false;
            if (!Physics.Raycast(mount, -up, rayLength, GameObject, out var hit) ||
                Vector3.Dot(hit.Normal, up) < 0.25f) continue;
            var otherBody = hit.Entity.GetComponent<RigidbodyComponent>();
            var isBall = hit.Entity.HasTag("ball");
            if (_jumpDetachTimer > 0.0f && !isBall) continue;
            if (otherBody is not null && !otherBody.IsKinematic && !isBall) continue;
            var relativeVelocity = _body!.GetVelocityAtPoint(mount) - (otherBody?.GetVelocityAtPoint(hit.Point) ?? Vector3.Zero);
            // Airborne wheels hang at full extension; their rubber can touch
            // the ball before the springs compress to their loaded ride height.
            var resetReach = chassisHeight * (suspensionRestLength + (isBall ? suspensionTravel : 0.08f));
            if (hit.Distance <= resetReach &&
                Vector3.Dot(relativeVelocity, hit.Normal) <= 1.0f)
                ResetWheelContacts++;
            // The ball can restore the flip, but cannot provide traction or
            // adhesion that pins it under an 850 kg car.
            if (isBall) continue;
            _wheelHits[wheel] = hit;
            _wheelTouching[wheel] = true;
            normalSum += hit.Normal;
            _wheelCount++;
        }
        if (_wheelCount > 0) SurfaceNormal = SafeDirection(normalSum, up);
        // Once the bank turns into the roof, release suspension and alignment.
        // The car keeps its momentum and falls under gravity instead of hanging.
        Grounded = _wheelCount >= 2 && SurfaceNormal.Y > -0.8f;
        _surfaceAngularVelocity = Grounded && _hadSurface
            ? ClampMagnitude(Vector3.Cross(_previousSurfaceNormal, SurfaceNormal) / dt, 12.0f)
            : Vector3.Zero;
        _previousSurfaceNormal = SurfaceNormal;
        _hadSurface = Grounded;
    }

    private void ApplySuspension(Vector3 forward, Vector3 velocity)
    {
        if (_body is null) return;
        var height = MathF.Max(MathF.Abs(GameObject.Scale.Y), 0.2f);
        var rest = height * suspensionRestLength;
        // Curved tracks need centripetal support proportional to speed squared.
        // Estimate normal curvature from contacts in the direction of travel,
        // so fast bank entry doesn't bottom out the chassis springs.
        var tangentVelocity = velocity - SurfaceNormal * Vector3.Dot(velocity, SurfaceNormal);
        var speed = tangentVelocity.Length();
        var direction = SafeDirection(tangentVelocity, forward);
        var curvature = 0.0f;
        for (var a = 0; a < 4; a++)
        for (var b = a + 1; b < 4; b++)
        {
            if (!_wheelTouching[a] || !_wheelTouching[b]) continue;
            var separation = _wheelHits[b].Point - _wheelHits[a].Point;
            var along = Vector3.Dot(separation, direction);
            if (MathF.Abs(along) < 0.1f || MathF.Abs(along) < separation.Length() * 0.7f) continue;
            curvature = MathF.Max(curvature, -Vector3.Dot(_wheelHits[b].Normal - _wheelHits[a].Normal, direction) / along);
        }
        var curveSupport = MathF.Min(speed * speed * curvature, 180.0f);
        var adhesion = wallAdhesion * (1.0f - MathF.Abs(SurfaceNormal.Y));
        _body.AddForce(-SurfaceNormal * adhesion * _body.Mass);
        for (var wheel = 0; wheel < 4; wheel++)
        {
            if (!_wheelTouching[wheel]) continue;
            var hit = _wheelHits[wheel];
            var pointVelocity = _body.GetVelocityAtPoint(_wheelMounts[wheel]);
            var compression = rest - hit.Distance;
            var spring = compression * suspensionStiffness - Vector3.Dot(pointVelocity, hit.Normal) * suspensionDamping;
            // Gravity/adhesion preload maintains ride height on floor and walls. A spring can push, but never pull the car to a track.
            var preload = MathF.Max(0.0f, 9.81f * SurfaceNormal.Y + adhesion);
            var force = Math.Clamp(spring + preload + curveSupport, 0.0f, 250.0f) * _body.Mass / _wheelCount;
            _body.AddForceAtPosition(hit.Normal * force, _wheelMounts[wheel]);
        }
    }

    private void ApplyTyres(Vector3 forward, Vector3 right)
    {
        if (_body is null || _wheelCount == 0) return;
        for (var wheel = 0; wheel < 4; wheel++)
        {
            if (!_wheelTouching[wheel]) continue;
            var normal = _wheelHits[wheel].Normal;
            var tyreForward = SafeDirection(forward - normal * Vector3.Dot(forward, normal), forward);
            var tyreRight = SafeDirection(Vector3.Cross(tyreForward, normal), right);
            var angle = wheel < 2 ? _steerAngle : 0.0f;
            tyreForward = tyreForward * MathF.Cos(angle) + tyreRight * MathF.Sin(angle);
            tyreRight = SafeDirection(Vector3.Cross(tyreForward, normal), right);
            var pointVelocity = _body.GetVelocityAtPoint(_wheelMounts[wheel]);
            var sideSpeed = Vector3.Dot(pointVelocity, tyreRight);
            var forwardSpeed = Vector3.Dot(pointVelocity, tyreForward);
            var grip = lateralGrip * (_powerslide ? (wheel < 2 ? powerslideFrontGrip : powerslideRearGrip) : 1.0f);
            var force = -tyreRight * Math.Clamp(sideSpeed * grip, -45.0f, 45.0f);
            var opposingMotion = _throttle * forwardSpeed < 0.0f;
            if (MathF.Abs(_throttle) > 0.001f && (MathF.Abs(forwardSpeed) < maximumSpeed || opposingMotion))
                force += tyreForward * (_throttle * acceleration * (opposingMotion ? 1.6f : 1.0f));
            if (MathF.Abs(_throttle) < 0.01f && !_boosting)
                force -= tyreForward * forwardSpeed * (_powerslide ? 0.1f : 0.6f);
            _body.AddForceAtPosition(force * (_body.Mass / 4.0f), _wheelMounts[wheel]);
        }
    }

    private void ApplyAngularAcceleration(Vector3 angularAcceleration, Vector3 right, Vector3 up, Vector3 forward)
    {
        if (_body is null) return;
        var size = Vector3.Abs(GameObject.Scale) * new Vector3(0.92f, 0.75f, 0.88f);
        var mass = _body.Mass;
        var lever = MathF.Max(controlLeverArm, 0.1f);
        // Box inertia converts desired angular acceleration into physical torque.
        var pitch = Vector3.Dot(angularAcceleration, right) * mass * (size.Y * size.Y + size.Z * size.Z) / 12.0f;
        var yaw = Vector3.Dot(angularAcceleration, up) * mass * (size.X * size.X + size.Z * size.Z) / 12.0f;
        var roll = Vector3.Dot(angularAcceleration, forward) * mass * (size.X * size.X + size.Y * size.Y) / 12.0f;
        ApplyForceCouple(right * MathF.Sign(pitch), forward, MathF.Abs(pitch) / (2.0f * lever));
        ApplyForceCouple(up * MathF.Sign(yaw), forward, MathF.Abs(yaw) / (2.0f * lever));
        ApplyForceCouple(forward * MathF.Sign(roll), right, MathF.Abs(roll) / (2.0f * lever));
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
        var force = Vector3.Normalize(forceDirection) * MathF.Max(forceMagnitude, 0.0f) * MathF.Min(torqueAxis.Length(), 1.0f);
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
