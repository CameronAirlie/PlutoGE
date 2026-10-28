using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// Two-player, physics-backed arcade car controller. Each player can use their
/// matching gamepad, with the original keyboard controls retained. The car's
/// team comes from its "blue"/"orange" scene tag. Presentation lives in
/// <see cref="CarVisuals"/>, which reads the state exposed here.
/// </summary>
public sealed class ArcadeCarController : ScriptBehaviour
{
    public const int WheelCount = 4;

    [SerializedField] private int playerNumber  = 1;
    // Throttle acceleration below top speed; it fades out as the car nears maximumSpeed.
    [SerializedField] private float acceleration  = 26.0f;
    // Throttle against the direction of travel.
    [SerializedField] private float brakeAcceleration = 30.0f;
    // Fraction of forward speed lost per second with no throttle.
    [SerializedField] private float coastDeceleration = 0.6f;
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

    [SerializedField] private float boostAcceleration  = 18.0f;
    // maximumSpeed is the unboosted drive limit. maximumBoostSpeed is also the
    // hard cap for total linear velocity from boost, impacts, and falling.
    [SerializedField] private float maximumSpeed  = 23.0f;
    [SerializedField] private float maximumBoostSpeed  = 36.0f;
    // Cap on air-control spin; flips may briefly exceed it.
    [SerializedField] private float maximumAngularSpeed  = 5.5f;
    // Side-slip removed per second at each tyre; powerslide scales it down.
    [SerializedField] private float lateralGrip  = 32.0f;
    // Most side force the tyres can supply, in m/s² (enough to hold a top-speed turn).
    [SerializedField] private float maximumGripAcceleration = 120.0f;
    // Turning curvature (1/turning radius) when stopped and at top boost speed.
    // Curvature falls off with the square of speed, so mid-speed turns stay tight.
    [SerializedField] private float lowSpeedCurvature = 0.5f;
    [SerializedField] private float highSpeedCurvature = 0.075f;
    [SerializedField] private float powerslideTurnBoost = 1.35f;
    // How quickly yaw follows the steering target, in 1/s.
    [SerializedField] private float steeringResponse = 14.0f;
    // Visual front-wheel angle only; turning comes from the curvature above.
    [SerializedField] private float maximumSteerAngle = 32.0f;
    [SerializedField] private float powerslideFrontGrip = 0.45f;
    [SerializedField] private float powerslideRearGrip = 0.08f;
    [SerializedField] private float flipWindow = 1.5f;
    [SerializedField] private float dodgeForwardImpulse = 9.0f;
    [SerializedField] private float dodgeSideImpulse = 9.0f;
    // Side dodges are this much stronger at top speed.
    [SerializedField] private float dodgeSideSpeedBonus = 0.9f;
    // A dodge spins at a fixed rate for flipDuration, then air control resumes.
    [SerializedField] private float flipSpinRate = 8.4f;
    [SerializedField] private float flipDuration = 0.65f;
    // After a dodge, gravity is held off this long so the dodge travels flat.
    [SerializedField] private float flipStallDuration = 0.3f;
    // Pulling the stick against a forward/backward dodge by more than this stops its pitch spin.
    // Stick held against a forward/backward dodge (beyond the dead zone)
    // decelerates its pitch spin at up to flipCancelAcceleration rad/s².
    [SerializedField] private float flipCancelDeadZone = 0.1f;
    [SerializedField] private float flipCancelAcceleration = 45.0f;
    // Jumping while resting on the roof or a side rolls the car back onto its wheels.
    [SerializedField] private float rightingImpulse = 3.5f;
    [SerializedField] private float rightingSpinRate = 9.0f;
    [SerializedField] private float rightingDuration = 0.8f;
    // Lying still off the wheels this long rights the car without any input.
    [SerializedField] private float autoRightDelay = 1.0f;
    [SerializedField] private float flipDeadZone = 0.5f;
    [SerializedField] private float airPitchAcceleration = 12.0f;
    [SerializedField] private float airYawAcceleration = 9.0f;
    [SerializedField] private float airRollAcceleration = 30.0f;
    [SerializedField] private float airPitchDamping = 6.0f;
    [SerializedField] private float airYawDamping = 5.0f;
    [SerializedField] private float airRollDamping = 4.5f;
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
    [SerializedField] private float controlLeverArm  = 1.35000002f;
    [SerializedField] private float jumpImpulse  = 7.5f;
    // Holding jump after a surface jump keeps pushing for a short time, so a
    // tap is a hop and a held press reaches noticeably higher.
    [SerializedField] private float jumpHoldAcceleration = 28.0f;
    [SerializedField] private float jumpHoldDuration = 0.2f;
    [SerializedField] private float jumpBufferDuration = 0.16f;
    // Throttle still nudges the car along its nose while airborne.
    [SerializedField] private float airThrottleAcceleration = 1.6f;
    // Supersonic is entered at supersonicSpeed and kept until the car slows
    // below supersonicSpeed - supersonicHysteresis.
    [SerializedField] private float supersonicSpeed = 33.0f;
    [SerializedField] private float supersonicHysteresis = 2.0f;
    // A supersonic nose hit closing faster than this demolishes an opponent.
    [SerializedField] private float demolitionClosingSpeed = 15.0f;
    [SerializedField] private float respawnDelay = 3.0f;
    [SerializedField] private float respawnHeight  = -4.0f;
    [SerializedField, InputMappingAsset] private string inputMappingAsset = "project://Input/RocketLeg.plutoinput";

    public int PlayerNumber => Math.Max(playerNumber, 1);
    public Vector3 Position => GameObject.WorldPosition;
    public Team Team { get; private set; } = Team.Blue;
    public bool IsSupersonic { get; private set; }
    public bool IsDemolished { get; private set; }
    public float RespawnTimeRemaining => IsDemolished ? _respawnTimer : 0.0f;
    /// <summary>Front-wheel steering angle in radians (positive steers right).</summary>
    public float SteerAngle => _steerAngle;
    public float Throttle => _throttle;
    /// <summary>Velocity at the end of the last fixed step, before contact resolution changed it.</summary>
    public Vector3 PreContactVelocity => _lastVelocity;
    /// <summary>Where a demolished car returns. The match moves it to the team's half.</summary>
    public Vector3 RespawnPosition { get; set; }
    public Vector3 RespawnRotation { get; set; }

    /// <summary>Raised on the victim; the argument is the car that demolished it.</summary>
    public event Action<ArcadeCarController>? Demolished;
    public event Action? Respawned;

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
    // Dodge spin as (right, forward) weights in the car's own axes.
    private Vector2 _flipSpin;
    private float _rightingTimer;
    private float _restingTimer;
    // Collider box size in chassis-scale units; replaced by the authored size in OnCreate.
    private Vector3 _colliderSize = new(0.92f, 0.75f, 0.88f);
    private Vector2 _dodgeDirection;
    private bool _boosting;
    private bool _jumpHeld;
    private bool _jumpQueued;
    private bool _jumpAvailable;
    private float _jumpBufferTimer;
    private float _groundedGraceTimer;
    private float _jumpDetachTimer;
    private float _jumpHoldTimer;
    private Vector3 _jumpHoldNormal = Vector3.UnitY;
    private float _respawnTimer;
    private bool _frozen;
    private Vector3 _lastVelocity;
    private readonly RaycastHit[] _wheelHits = new RaycastHit[WheelCount];
    private readonly Vector3[] _wheelMounts = new Vector3[WheelCount];
    private readonly bool[] _wheelTouching = new bool[WheelCount];
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

        Team = GameObject.HasTag(Team.Orange.Tag()) ? Team.Orange : Team.Blue;
        _spawnPosition = GameObject.WorldPosition;
        _spawnRotation = GameObject.WorldRotation;
        RespawnPosition = _spawnPosition;
        RespawnRotation = _spawnRotation;
        _body = GameObject.GetComponent<RigidbodyComponent>();
        if (_body is null)
        {
            Debug.LogError($"{GameObject.Name} needs a RigidbodyComponent.");
            return;
        }

        // Mass, drag, friction and the collider are authored on the components
        // and used as they are. The handling assumes zero angular drag (air
        // control damps rotation itself) and low chassis friction (grip comes
        // from the tyre model, and the belly must slide over mesh seams).
        if (_body.AngularDrag > 0.0f || _body.Friction > 0.2f)
            Debug.LogWarning($"{GameObject.Name}: the car handling expects Angular Drag 0 and Friction at most 0.2 " +
                             $"(found {_body.AngularDrag} and {_body.Friction}).");
        // The collider box (in chassis-scale units) sets the inertia used to turn
        // alignment into torque; it is read, never written.
        if (GameObject.GetComponent<ColliderComponent>() is { Shape: ColliderShape.Box } collider)
            _colliderSize = Vector3.Max(Vector3.Abs(collider.Size), new Vector3(0.01f));
        _body.UseGravity = true;
        _body.IsKinematic = false;
        _body.FreezeRotation = false;
    }

    public override void OnUpdate(float deltaTime)
    {
        if (IsDemolished)
        {
            _respawnTimer -= deltaTime;
            if (_respawnTimer <= 0.0f) Respawn();
            return;
        }

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

        KeepInsideArena();
        _body.AddForce(-Vector3.UnitY * (Gravity.Extra * _body.Mass));
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
            _rightingTimer = 0.0f;
        }
        else
        {
            _flipTimeRemaining = MathF.Max(0.0f, _flipTimeRemaining - fixedDeltaTime);
        }
        _restingTimer = !grounded && _rightingTimer <= 0.0f && IsRestingOffWheels(up, velocity)
            ? _restingTimer + fixedDeltaTime : 0.0f;
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
            _jumpHoldTimer = MathF.Max(jumpHoldDuration, 0.0f);
            _jumpHoldNormal = jumpNormal;
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
        else if (!grounded && _rightingTimer <= 0.0f && (_jumpQueued || _restingTimer >= autoRightDelay) &&
                 IsRestingOffWheels(up, velocity))
        {
            // Stuck on the roof or a side: jump (or wait a moment) to hop and
            // roll back onto the wheels.
            var hop = Vector3.UnitY * rightingImpulse;
            _body.AddImpulse(hop * _body.Mass);
            velocity += hop;
            _rightingTimer = MathF.Max(rightingDuration, 0.05f);
            _restingTimer = 0.0f;
            _jumpQueued = false;
            _jumpBufferTimer = 0.0f;
        }
        else if (_jumpQueued && !grounded && FlipAvailable && !IsFlipping)
        {
            var direction = _queuedJumpDirection;
            var impulse = up * jumpImpulse;
            if (direction.LengthSquared() >= flipDeadZone * flipDeadZone)
            {
                // Dodges push along the heading flattened onto the ground, so a
                // nose-up car still dodges forwards rather than into the sky.
                direction = direction.LengthSquared() > 1.0f ? Vector2.Normalize(direction) : direction;
                var flatForward = SafeDirection(Flatten(forward), Flatten(-up));
                var flatRight = Vector3.Cross(flatForward, Vector3.UnitY);
                var speedFraction = Vector3.Dot(velocity, flatForward) / MathF.Max(maximumBoostSpeed, 1.0f);
                var push = CarHandling.DodgeImpulse(SafeDirection(direction, Vector2.UnitY), speedFraction,
                                                    dodgeForwardImpulse, dodgeSideImpulse, dodgeSideSpeedBonus);
                // A dodge cancels vertical motion: it travels flat instead of
                // carrying the jump's rise or a fall.
                impulse = flatForward * push.Y + flatRight * push.X - Vector3.UnitY * velocity.Y;
                _dodgeDirection = SafeDirection(direction, Vector2.UnitY);
                _flipSpin = CarHandling.DodgeSpin(_dodgeDirection) * flipSpinRate;
                _flipElapsed = 0.0f;
            }
            _body.AddImpulse(impulse * _body.Mass);
            velocity += impulse;
            _jumpHoldTimer = 0.0f;
            _airJumpAvailable = false;
            _jumpAvailable = false;
            _jumpQueued = false;
            _jumpBufferTimer = 0.0f;
            _groundedGraceTimer = 0.0f;
            _jumpDetachTimer = 0.20f;
        }

        if (_jumpHoldTimer > 0.0f)
        {
            // Releasing jump ends the extra push; it cannot be resumed.
            if (_jumpHeld && !IsFlipping) _body.AddForce(_jumpHoldNormal * jumpHoldAcceleration * _body.Mass);
            _jumpHoldTimer = _jumpHeld ? MathF.Max(0.0f, _jumpHoldTimer - fixedDeltaTime) : 0.0f;
        }

        if (IsFlipping) grounded = Grounded = false;
        var steerLimit = maximumSteerAngle * MathF.PI / 180.0f *
            (1.0f - 0.55f * Math.Clamp(velocity.Length() / MathF.Max(maximumBoostSpeed, 1.0f), 0.0f, 1.0f));
        _steerAngle += (_steering * steerLimit - _steerAngle) * (1.0f - MathF.Exp(-steeringResponse * fixedDeltaTime));

        if (grounded)
        {
            ApplySuspension(forward, velocity);
            var normal = SurfaceNormal;
            ApplyTyres(forward, right, velocity);
            ApplySteering(normal, forward, velocity, fixedDeltaTime);
            var yaw = Vector3.Dot(_body.AngularVelocity, normal);
            var tiltVelocity = _body.AngularVelocity - normal * yaw;
            var alignmentAcceleration = Vector3.Cross(up, normal) * surfaceAlignment +
                (_surfaceAngularVelocity - tiltVelocity) * surfaceDamping;
            // Surface alignment supplies pitch/roll only; yaw is set by ApplySteering.
            ApplyAngularAcceleration(alignmentAcceleration, right, up, forward);
        }
        else if (IsFlipping)
        {
            UpdateFlip(right, forward, fixedDeltaTime);
        }
        else if (_rightingTimer > 0.0f)
        {
            UpdateRighting(up, forward, fixedDeltaTime);
        }
        else
        {
            // Stick Y pitches the nose. Stick X yaws normally, but while the
            // left bumper is held it produces roll instead (free air roll).
            // The air-roll buttons add roll in either mode.
            var aerialYaw = _freeAirRoll ? 0.0f : _steering;
            var aerialRoll = Math.Clamp(_roll + (_freeAirRoll ? _steering : 0.0f), -1.0f, 1.0f);
            var tuning = new AirControlTuning(new Vector3(airPitchAcceleration, airYawAcceleration, airRollAcceleration),
                                              new Vector3(airPitchDamping, airYawDamping, airRollDamping));
            // Local axes: +pitch about right (nose up), +yaw about -up (nose right), +roll about forward (roll right).
            var angular = _body.AngularVelocity;
            var local = new Vector3(Vector3.Dot(angular, right), Vector3.Dot(angular, -up), Vector3.Dot(angular, forward));
            local = CarHandling.AirControl(local, new Vector3(_pitch, aerialYaw, aerialRoll), tuning, fixedDeltaTime);
            _body.AngularVelocity = right * local.X - up * local.Y + forward * local.Z;
            if (MathF.Abs(_throttle) > 0.01f && Vector3.Dot(velocity, forward) * _throttle < maximumSpeed)
                _body.AddForce(forward * (_throttle * airThrottleAcceleration * _body.Mass));
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

        // Forces and collisions still determine both momentum vectors. Only
        // their magnitudes are capped, preserving combined pitch/yaw/roll.
        _lastVelocity = ClampMagnitude(velocity, MathF.Max(maximumBoostSpeed, 0.0f));
        _body.Velocity = _lastVelocity;
        _body.AngularVelocity = ClampMagnitude(_body.AngularVelocity, IsFlipping
            ? MathF.Max(maximumAngularSpeed, flipSpinRate)
            : MathF.Max(maximumAngularSpeed, 0.0f));

        var speed = _lastVelocity.Length();
        IsSupersonic = speed >= supersonicSpeed || (IsSupersonic && speed >= supersonicSpeed - supersonicHysteresis);
    }

    public override void OnCollisionEnter(GameObject other)
    {
        // Contacts report after the solver; use the velocity this car carried
        // into the step so a full-speed hit is judged at full speed.
        if (!IsSupersonic || IsDemolished || !other.HasTag("car")) return;
        var victim = other.GetComponent<ArcadeCarController>();
        if (victim is null || victim.IsDemolished || victim.Team == Team) return;
        var toVictim = SafeDirection(other.WorldPosition - GameObject.WorldPosition, GameObject.Forward);
        if (Vector3.Dot(toVictim, SafeDirection(GameObject.Forward, toVictim)) < 0.3f) return;
        if (Vector3.Dot(_lastVelocity - victim._lastVelocity, toVictim) < demolitionClosingSpeed) return;
        victim.Demolish(this);
    }

    /// <summary>
    /// Removes the car from play for <c>respawnDelay</c> seconds, then returns
    /// it at <see cref="RespawnPosition"/> with a full tank.
    /// </summary>
    public void Demolish(ArcadeCarController attacker)
    {
        if (IsDemolished || _body is null) return;
        IsDemolished = true;
        IsSupersonic = false;
        IsBoosting = false;
        _respawnTimer = MathF.Max(respawnDelay, 0.0f);
        _body.Velocity = Vector3.Zero;
        _body.AngularVelocity = Vector3.Zero;
        _body.IsKinematic = true;
        // Park the wreck above the stadium roof, clear of every collider.
        GameObject.WorldPosition = new Vector3(0.0f, 120.0f + PlayerNumber * 8.0f, 0.0f);
        Demolished?.Invoke(attacker);
    }

    private void Respawn()
    {
        ResetCar(RespawnPosition, RespawnRotation);
        if (_body is not null) _body.IsKinematic = _frozen;
        Respawned?.Invoke();
    }

    /// <summary>
    /// Suspension length at a wheel in chassis-height units: the contact
    /// distance when touching, or full extension when the wheel hangs free.
    /// </summary>
    public float GetWheelExtension(int wheel)
    {
        var full = suspensionRestLength + suspensionTravel;
        if (wheel < 0 || wheel >= WheelCount || !_wheelTouching[wheel]) return full;
        var height = MathF.Max(MathF.Abs(GameObject.Scale.Y), 0.2f);
        return Math.Clamp(_wheelHits[wheel].Distance / height, 0.0f, full);
    }

    public bool IsWheelTouching(int wheel) => wheel >= 0 && wheel < WheelCount && _wheelTouching[wheel];

    public void ResetCar()
    {
        ResetCar(_spawnPosition, _spawnRotation);
    }

    public void ResetCar(Vector3 worldPosition, Vector3 worldRotation)
    {
        BoostAmount = MathF.Max(boostCapacity, 1.0f);
        IsBoosting = false;
        IsSupersonic = false;
        IsDemolished = false;
        _respawnTimer = 0.0f;
        _lastVelocity = Vector3.Zero;
        _jumpHoldTimer = 0.0f;
        _rightingTimer = 0.0f;
        _restingTimer = 0.0f;
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
        _frozen = frozen;
        IsBoosting = false;
        if (_body is null) return;
        _body.Velocity = Vector3.Zero;
        _body.AngularVelocity = Vector3.Zero;
        // A wreck stays parked until it respawns, even when play resumes.
        _body.IsKinematic = frozen || IsDemolished;
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
            var preload = MathF.Max(0.0f, Gravity.Stadium * SurfaceNormal.Y + adhesion);
            var force = Math.Clamp(spring + preload + curveSupport, 0.0f, 250.0f) * _body.Mass / _wheelCount;
            _body.AddForceAtPosition(hit.Normal * force, _wheelMounts[wheel]);
        }
    }

    private void ApplyTyres(Vector3 forward, Vector3 right, Vector3 velocity)
    {
        if (_body is null || _wheelCount == 0) return;
        for (var wheel = 0; wheel < 4; wheel++)
        {
            if (!_wheelTouching[wheel]) continue;
            var normal = _wheelHits[wheel].Normal;
            var tyreForward = SafeDirection(forward - normal * Vector3.Dot(forward, normal), forward);
            // Grip acts along the chassis axes, not the steered wheels, and on the
            // centre of mass using its velocity: it only removes side-slip and
            // never adds or resists yaw. Turning is entirely ApplySteering's.
            var tyreRight = SafeDirection(Vector3.Cross(tyreForward, normal), right);
            var sideSpeed = Vector3.Dot(velocity, tyreRight);
            var forwardSpeed = Vector3.Dot(velocity, tyreForward);
            var grip = lateralGrip * (_powerslide ? (wheel < 2 ? powerslideFrontGrip : powerslideRearGrip) : 1.0f);
            var force = -tyreRight * Math.Clamp(sideSpeed * grip, -maximumGripAcceleration, maximumGripAcceleration);
            if (MathF.Abs(_throttle) >= 0.01f || !_boosting)
                force += tyreForward * CarHandling.DriveAcceleration(_throttle, forwardSpeed, maximumSpeed, acceleration,
                    brakeAcceleration, _powerslide ? coastDeceleration * 0.15f : coastDeceleration);
            _body.AddForce(force * (_body.Mass / 4.0f));
        }
    }

    /// <summary>
    /// Drives yaw about the surface normal towards the rate the steering
    /// curvature implies. Authority scales with the number of front tyres in
    /// contact, so a car on its rear wheels alone cannot turn.
    /// </summary>
    private void ApplySteering(Vector3 normal, Vector3 forward, Vector3 velocity, float deltaTime)
    {
        if (_body is null) return;
        var frontContacts = (_wheelTouching[0] ? 1 : 0) + (_wheelTouching[1] ? 1 : 0);
        if (frontContacts == 0) return;
        var forwardSpeed = Vector3.Dot(velocity, SafeDirection(forward - normal * Vector3.Dot(forward, normal), forward));
        var curvature = CarHandling.Curvature(MathF.Abs(forwardSpeed) / MathF.Max(maximumBoostSpeed, 1.0f),
                                              lowSpeedCurvature, highSpeedCurvature);
        if (_powerslide) curvature *= powerslideTurnBoost;
        var target = CarHandling.TargetYawRate(_steering, forwardSpeed, curvature);
        var angular = _body.AngularVelocity;
        var yaw = Vector3.Dot(angular, normal);
        var response = (1.0f - MathF.Exp(-MathF.Max(steeringResponse, 0.0f) * deltaTime)) * frontContacts * 0.5f;
        _body.AngularVelocity = angular + normal * ((target - yaw) * response);
    }

    /// <summary>
    /// A dodge spins at a fixed rate about the axis chosen at its start, then
    /// hands back to air control. Pulling the stick against a forward or
    /// backward dodge cancels its pitch spin; any roll part continues.
    /// </summary>
    private void UpdateFlip(Vector3 right, Vector3 forward, float deltaTime)
    {
        if (_body is null) return;
        var stickForward = -_pitch;
        // Holding the stick against the dodge pushes back on its pitch spin
        // (harder with more stick) until it stops; it never reverses it.
        var against = MathF.Abs(_dodgeDirection.Y) > 0.1f ? -stickForward * MathF.Sign(_dodgeDirection.Y) : 0.0f;
        if (against > flipCancelDeadZone && _flipSpin.X != 0.0f)
        {
            var slowed = MathF.Abs(_flipSpin.X) - MathF.Min(against, 1.0f) * flipCancelAcceleration * deltaTime;
            _flipSpin.X = MathF.Sign(_flipSpin.X) * MathF.Max(slowed, 0.0f);
        }
        _body.AngularVelocity = right * _flipSpin.X + forward * _flipSpin.Y;
        // Hold the dodge level for a moment instead of letting it fall at once.
        if (_flipElapsed < flipStallDuration) _body.AddForce(Vector3.UnitY * (Gravity.Stadium * _body.Mass));
        _flipElapsed += deltaTime;
        if (_flipElapsed >= MathF.Max(flipDuration, 0.05f)) _flipElapsed = -1.0f;
    }

    /// <summary>
    /// On the roof, a side, or tipped well over, slow, and lying on the floor:
    /// it is within a side's half-width below the chassis and the car is not
    /// rising or falling. A car rocking or sliding on its roof still counts; a
    /// tilted car in mid-jump does not, so its dodge is never taken away.
    /// </summary>
    private bool IsRestingOffWheels(Vector3 up, Vector3 velocity) =>
        up.Y < 0.7f && velocity.LengthSquared() < 100.0f && MathF.Abs(velocity.Y) < 2.0f &&
        Physics.Raycast(GameObject.WorldPosition, -Vector3.UnitY, 0.75f, GameObject, out _);

    /// <summary>Rolls the car's up vector back towards the sky, easing off as it arrives.</summary>
    private void UpdateRighting(Vector3 up, Vector3 forward, float deltaTime)
    {
        if (_body is null) return;
        _rightingTimer = MathF.Max(0.0f, _rightingTimer - deltaTime);
        var angle = MathF.Acos(Math.Clamp(up.Y, -1.0f, 1.0f));
        if (angle < 0.15f)
        {
            _rightingTimer = 0.0f;
            return;
        }
        // Fully upside down, any horizontal axis works; roll about the nose.
        var axis = SafeDirection(Vector3.Cross(up, Vector3.UnitY), forward);
        _body.AngularVelocity = axis * MathF.Min(rightingSpinRate, angle * 10.0f);
    }

    private static Vector3 Flatten(Vector3 value) => new(value.X, 0.0f, value.Z);

    /// <summary>
    /// Safety net for the thin stadium mesh: a chassis that slips through it
    /// (crawling over the roof edge, say) is put back inside, at ride height
    /// from the surface, without its outward velocity.
    /// </summary>
    private void KeepInsideArena()
    {
        if (_body is null || !ArenaLayout.TryContain(GameObject.WorldPosition, 0.4f, out var inside, out var inward)) return;
        GameObject.WorldPosition = inside;
        var velocity = _body.Velocity;
        var outward = Vector3.Dot(velocity, inward);
        if (outward < 0.0f) _body.Velocity = velocity - inward * outward;
    }

    private void ApplyAngularAcceleration(Vector3 angularAcceleration, Vector3 right, Vector3 up, Vector3 forward)
    {
        if (_body is null) return;
        var size = Vector3.Abs(GameObject.Scale) * _colliderSize;
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

    private static Vector2 SafeDirection(Vector2 value, Vector2 fallback)
    {
        return value.LengthSquared() > 0.0001f ? Vector2.Normalize(value) : fallback;
    }

    private static Vector3 ClampMagnitude(Vector3 value, float maximumLength)
    {
        var lengthSquared = value.LengthSquared();
        if (lengthSquared <= maximumLength * maximumLength) return value;
        return Vector3.Normalize(value) * maximumLength;
    }
}
