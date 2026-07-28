using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

public readonly record struct FpsAmmoState(int Magazine, int Reserve, int MagazineSize, bool IsReloading);

public readonly record struct FpsMovementState(
    bool IsGrounded,
    bool IsAiming,
    bool IsSprinting,
    bool IsCrouching,
    bool IsSliding);

public readonly record struct FpsHitEvent(
    GameObject Target,
    Vector3 Point,
    float Damage,
    bool IsHeadshot);

/// <summary>
/// A responsive, self-contained first-person controller and hitscan weapon.
/// Optional scene references add a view model, effects, audio, animation and HUD.
/// </summary>
public sealed class KinematicFpsController : ScriptBehaviour
{
    /// <summary>Raised only when an ammo or reload value changes.</summary>
    public event Action<FpsAmmoState>? AmmoChanged;

    /// <summary>Raised only when one of the exposed movement flags changes.</summary>
    public event Action<FpsMovementState>? MovementStateChanged;

    /// <summary>Raised after a round is successfully fired.</summary>
    public event Action<int>? WeaponFired;

    /// <summary>Raised when a fired round damages a target.</summary>
    public event Action<FpsHitEvent>? HitConfirmed;

    /// <summary>Raised when the interactable under the crosshair changes.</summary>
    public event Action<GameObject?>? InteractionTargetChanged;

    public int Ammo => _ammo;
    public int ReserveAmmo => _reserveAmmo;
    public int MagazineSize => magazineSize;
    public bool IsReloading => _reloading;
    public bool IsGrounded => _grounded;
    public bool IsAiming => _aiming;
    public bool IsSprinting => _sprinting;
    public bool IsCrouching => _crouching;
    public bool IsSliding => _sliding;

    // Scene references
    [SerializedField] private CameraComponent? camera = null;
    [SerializedField] private GameObject? weaponModel = null;
    [SerializedField] private AnimationComponent? weaponAnimator = null;
    [SerializedField] private ParticleSystemComponent? muzzleFlash = null;
    [SerializedField] private SoundEmitterComponent? shotAudio = null;
    [SerializedField] private SoundEmitterComponent? reloadAudio = null;
    [SerializedField] private SoundEmitterComponent? emptyAudio = null;
    [SerializedField] private UITextComponent? ammoText = null;
    [SerializedField] private UITextComponent? promptText = null;

    // Look and camera
    [SerializedField] private float mouseSensitivity = 0.10f;
    [SerializedField] private float adsSensitivityMultiplier = 0.72f;
    [SerializedField] private float minimumPitch = -88.0f;
    [SerializedField] private float maximumPitch = 88.0f;
    [SerializedField] private float hipFov = 78.0f;
    [SerializedField] private float adsFov = 60.0f;
    [SerializedField] private float fovSharpness = 14.0f;
    [SerializedField] private float standingCameraHeight = 1.62f;
    [SerializedField] private float crouchingCameraHeight = 1.12f;
    [SerializedField] private float cameraHeightSharpness = 16.0f;

    // Movement
    [SerializedField] private float walkSpeed = 5.4f;
    [SerializedField] private float sprintSpeed = 8.0f;
    [SerializedField] private float crouchSpeed = 3.1f;
    [SerializedField] private float adsSpeedMultiplier = 0.72f;
    [SerializedField] private float groundAcceleration = 38.0f;
    [SerializedField] private float groundDeceleration = 30.0f;
    [SerializedField] private float airAcceleration = 9.0f;
    [SerializedField] private float jumpHeight = 1.15f;
    [SerializedField] private float gravity = -26.0f;
    [SerializedField] private float groundCheckDistance = 1.12f;
    [SerializedField] private float groundProbeOffset = 0.12f;
    [SerializedField] private float skinWidth = 0.03f;
    [SerializedField] private float slideEntrySpeed = 6.5f;
    [SerializedField] private float slideDuration = 0.72f;
    [SerializedField] private float slideFriction = 8.0f;

    // Weapon
    [SerializedField] private bool automaticFire = true;
    [SerializedField] private int magazineSize = 30;
    [SerializedField] private int startingReserveAmmo = 120;
    [SerializedField] private float roundsPerMinute = 720.0f;
    [SerializedField] private float reloadDuration = 1.75f;
    [SerializedField] private float damage = 30.0f;
    [SerializedField] private float headshotMultiplier = 1.5f;
    [SerializedField] private float range = 180.0f;
    [SerializedField] private float hipSpreadDegrees = 1.25f;
    [SerializedField] private float adsSpreadDegrees = 0.18f;
    [SerializedField] private float movementSpreadDegrees = 0.8f;
    [SerializedField] private float recoilPitch = 0.75f;
    [SerializedField] private float recoilYaw = 0.32f;
    [SerializedField] private float recoilRecovery = 9.0f;
    [SerializedField] private string damageableTag = "Enemy";
    [SerializedField] private string headTag = "Head";
    [SerializedField] private string damageMethod = "TakeDamage";

    // View-model motion
    [SerializedField] private Vector3 hipPosition = new(0.28f, -0.24f, -0.48f);
    [SerializedField] private Vector3 adsPosition = new(0.0f, -0.16f, -0.34f);
    [SerializedField] private float weaponPositionSharpness = 18.0f;
    [SerializedField] private float weaponSway = 0.0018f;
    [SerializedField] private float bobFrequency = 9.0f;
    [SerializedField] private float bobAmount = 0.018f;

    // Interaction
    [SerializedField] private float interactionDistance = 3.0f;
    [SerializedField] private string interactableTag = "Interactable";
    [SerializedField] private string interactMethod = "Interact";

    private RigidbodyComponent? _rigidbody;
    private ColliderComponent? _collider;
    private float _yaw;
    private float _pitch;
    private float _verticalVelocity;
    private Vector3 _horizontalVelocity;
    private Vector3 _slideVelocity;
    private Vector3 _weaponVelocity;
    private Vector3 _weaponRestRotation;
    private float _cameraHeight;
    private float _slideTime;
    private float _shotCooldown;
    private float _reloadTime;
    private float _bobTime;
    private float _recoilPitchOffset;
    private float _recoilYawOffset;
    private int _ammo;
    private int _reserveAmmo;
    private uint _randomState = 0xA341316Cu;
    private bool _grounded;
    private bool _crouching;
    private bool _sliding;
    private bool _aiming;
    private bool _sprinting;
    private bool _reloading;
    private FpsAmmoState _publishedAmmoState;
    private FpsMovementState _publishedMovementState;
    private uint _interactionTargetId;

    public override void OnCreate()
    {
        Input.CursorLocked = true;
        _yaw = Rotation.Y;
        _pitch = camera?.GameObject.Rotation.X ?? 0.0f;
        _cameraHeight = standingCameraHeight;
        _ammo = Math.Max(1, magazineSize);
        _reserveAmmo = Math.Max(0, startingReserveAmmo);

        _rigidbody = GameObject.GetComponent<RigidbodyComponent>();
        if (_rigidbody is not null)
        {
            _rigidbody.IsKinematic = true;
            _rigidbody.UseGravity = false;
            _rigidbody.FreezeRotation = true;
        }

        _collider = GameObject.GetComponent<ColliderComponent>();
        if (_collider is not null)
        {
            _collider.Shape = ColliderShape.Capsule;
        }

        if (weaponModel is not null)
        {
            _weaponRestRotation = weaponModel.Rotation;
            weaponModel.Position = hipPosition;
        }

        UpdateHud();
        _publishedAmmoState = CurrentAmmoState();
        _publishedMovementState = CurrentMovementState();
    }

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0.0f)
        {
            return;
        }

        if (Input.IsKeyPressed(KeyCode.Escape))
        {
            Input.CursorLocked = !Input.CursorLocked;
        }

        _shotCooldown = MathF.Max(0.0f, _shotCooldown - deltaTime);
        UpdateReload(deltaTime);
        UpdateLook(deltaTime);
        UpdateMovement(deltaTime);
        UpdateCamera(deltaTime);
        UpdateWeapon(deltaTime);
        UpdateInteraction();
        PublishStateChanges();
    }

    private void UpdateLook(float deltaTime)
    {
        _recoilPitchOffset = Damp(_recoilPitchOffset, 0.0f, recoilRecovery, deltaTime);
        _recoilYawOffset = Damp(_recoilYawOffset, 0.0f, recoilRecovery, deltaTime);
        if (!Input.CursorLocked)
        {
            return;
        }

        var sensitivity = mouseSensitivity * (_aiming ? adsSensitivityMultiplier : 1.0f);
        var mouse = Input.MouseDelta;
        _yaw -= mouse.X * sensitivity;
        _pitch = Math.Clamp(_pitch - mouse.Y * sensitivity, minimumPitch, maximumPitch);
        Rotation = new Vector3(0.0f, _yaw + _recoilYawOffset, 0.0f);
    }

    private void UpdateMovement(float deltaTime)
    {
        _grounded = CheckGrounded();
        if (_grounded && _verticalVelocity < 0.0f)
        {
            _verticalVelocity = -2.0f;
        }

        var input = Vector2.Zero;
        if (Input.IsKeyDown(KeyCode.W)) input.Y += 1.0f;
        if (Input.IsKeyDown(KeyCode.S)) input.Y -= 1.0f;
        if (Input.IsKeyDown(KeyCode.D)) input.X += 1.0f;
        if (Input.IsKeyDown(KeyCode.A)) input.X -= 1.0f;
        if (input.LengthSquared() > 1.0f) input = Vector2.Normalize(input);

        var forward = GameObject.Forward;
        var right = GameObject.Right;
        forward.Y = 0.0f;
        right.Y = 0.0f;
        if (forward.LengthSquared() > 0.001f) forward = Vector3.Normalize(forward);
        if (right.LengthSquared() > 0.001f) right = Vector3.Normalize(right);
        var moveDirection = right * input.X + forward * input.Y;
        if (moveDirection.LengthSquared() > 1.0f) moveDirection = Vector3.Normalize(moveDirection);

        _aiming = Input.CursorLocked && Input.IsMouseButtonDown(MouseButton.Right) && !_reloading;
        var wantsCrouch = Input.IsKeyDown(KeyCode.LeftControl) || _sliding;
        var canSprint = input.Y > 0.1f && !_aiming && !_reloading && !_crouching;
        _sprinting = _grounded && Input.IsKeyDown(KeyCode.LeftShift) && canSprint;

        if (_grounded && Input.IsKeyPressed(KeyCode.LeftControl) &&
            _sprinting && _horizontalVelocity.Length() >= slideEntrySpeed)
        {
            _sliding = true;
            _slideTime = slideDuration;
            _slideVelocity = Vector3.Normalize(_horizontalVelocity) *
                             MathF.Max(_horizontalVelocity.Length(), slideEntrySpeed);
            CancelReload();
        }

        if (_sliding)
        {
            _slideTime -= deltaTime;
            _slideVelocity = MoveTowards(_slideVelocity, Vector3.Zero, slideFriction * deltaTime);
            _horizontalVelocity = _slideVelocity;
            if (_slideTime <= 0.0f || !_grounded || _slideVelocity.LengthSquared() < 4.0f)
            {
                _sliding = false;
            }
        }
        else
        {
            _crouching = wantsCrouch;
            var speed = _crouching ? crouchSpeed : (_sprinting ? sprintSpeed : walkSpeed);
            if (_aiming) speed *= adsSpeedMultiplier;
            var targetVelocity = moveDirection * speed;
            var acceleration = !_grounded
                ? airAcceleration
                : (input.LengthSquared() > 0.0f ? groundAcceleration : groundDeceleration);
            _horizontalVelocity = MoveTowards(_horizontalVelocity, targetVelocity, acceleration * deltaTime);
        }

        _crouching = wantsCrouch;
        if (_grounded && Input.IsKeyPressed(KeyCode.Space) && !_sliding)
        {
            _verticalVelocity = MathF.Sqrt(jumpHeight * -2.0f * gravity);
            _grounded = false;
            CancelReload();
        }
        _verticalVelocity += gravity * deltaTime;

        Physics.MoveKinematic(GameObject, _horizontalVelocity * deltaTime, skinWidth);
        var vertical = new Vector3(0.0f, _verticalVelocity * deltaTime, 0.0f);
        var applied = Physics.MoveKinematic(GameObject, vertical, skinWidth);
        if (vertical.Y < 0.0f && MathF.Abs(applied.Y) < MathF.Abs(vertical.Y) * 0.5f)
        {
            _verticalVelocity = -2.0f;
        }
    }

    private void UpdateCamera(float deltaTime)
    {
        if (camera is null)
        {
            return;
        }

        var desiredHeight = (_crouching || _sliding) ? crouchingCameraHeight : standingCameraHeight;
        _cameraHeight = Damp(_cameraHeight, desiredHeight, cameraHeightSharpness, deltaTime);
        var localPosition = camera.GameObject.Position;
        localPosition.Y = _cameraHeight;
        camera.GameObject.Position = localPosition;
        camera.GameObject.Rotation = new Vector3(_pitch + _recoilPitchOffset, 0.0f, 0.0f);

        var desiredFov = _aiming ? adsFov : hipFov;
        if (_sprinting) desiredFov += 5.0f;
        camera.Fov = Damp(camera.Fov, desiredFov, fovSharpness, deltaTime);
    }

    private void UpdateWeapon(float deltaTime)
    {
        if (Input.IsKeyPressed(KeyCode.R))
        {
            BeginReload();
        }

        var fireHeld = automaticFire
            ? Input.IsMouseButtonDown(MouseButton.Left)
            : Input.IsMouseButtonPressed(MouseButton.Left);
        if (Input.CursorLocked && fireHeld && !_sprinting && !_sliding)
        {
            TryFire();
        }

        if (weaponModel is null)
        {
            return;
        }

        var moving = _grounded && _horizontalVelocity.LengthSquared() > 0.5f;
        if (moving) _bobTime += deltaTime * bobFrequency * (_sprinting ? 1.35f : 1.0f);
        var bobScale = moving && !_aiming ? bobAmount : 0.0f;
        var bob = new Vector3(MathF.Cos(_bobTime) * bobScale, MathF.Abs(MathF.Sin(_bobTime)) * bobScale, 0.0f);
        var sway = Input.MouseDelta * weaponSway;
        var target = (_aiming ? adsPosition : hipPosition) + bob + new Vector3(sway.X, -sway.Y, 0.0f);
        weaponModel.Position = SmoothDamp(weaponModel.Position, target, ref _weaponVelocity,
            1.0f / MathF.Max(weaponPositionSharpness, 0.01f), deltaTime);
        weaponModel.Rotation = _weaponRestRotation + new Vector3(-sway.Y * 20.0f, sway.X * 20.0f, -bob.X * 180.0f);
    }

    private void TryFire()
    {
        if (_reloading || _shotCooldown > 0.0f)
        {
            return;
        }
        if (_ammo <= 0)
        {
            _shotCooldown = 0.18f;
            emptyAudio?.PlayOneShot();
            BeginReload();
            return;
        }

        _ammo--;
        _shotCooldown = 60.0f / MathF.Max(1.0f, roundsPerMinute);
        shotAudio?.PlayOneShot(1.0f, 0.97f + NextFloat() * 0.06f);
        muzzleFlash?.Emit(1);
        weaponAnimator?.SetTrigger("Fire");
        WeaponFired?.Invoke(_ammo);

        var movingSpread = _horizontalVelocity.LengthSquared() > 0.5f ? movementSpreadDegrees : 0.0f;
        var spread = (_aiming ? adsSpreadDegrees : hipSpreadDegrees) + movingSpread;
        var direction = ApplySpread(camera?.GameObject.Forward ?? GameObject.Forward, spread);
        var origin = camera?.GameObject.WorldPosition ?? GameObject.WorldPosition;
        if (Physics.Raycast(origin, direction, range, GameObject, out var hit) &&
            (hit.Entity.HasTag(damageableTag) || hit.Entity.HasTag(headTag)))
        {
            var isHeadshot = hit.Entity.HasTag(headTag);
            var dealtDamage = damage * (isHeadshot ? headshotMultiplier : 1.0f);
            if (!hit.Entity.TryInvoke(damageMethod, dealtDamage))
            {
                Debug.LogWarning($"Hit {hit.Entity.Name}, but {damageMethod}(float) was not found.");
            }
            else
            {
                HitConfirmed?.Invoke(new FpsHitEvent(hit.Entity, hit.Point, dealtDamage, isHeadshot));
            }
        }

        _recoilPitchOffset -= recoilPitch * (0.85f + NextFloat() * 0.3f);
        _recoilYawOffset += (NextFloat() * 2.0f - 1.0f) * recoilYaw;
        UpdateHud();
    }

    private void BeginReload()
    {
        if (_reloading || _ammo >= magazineSize || _reserveAmmo <= 0 || _sliding)
        {
            return;
        }
        _reloading = true;
        _reloadTime = MathF.Max(0.05f, reloadDuration);
        reloadAudio?.PlayOneShot();
        weaponAnimator?.SetTrigger("Reload");
        UpdateHud();
    }

    private void UpdateReload(float deltaTime)
    {
        if (!_reloading)
        {
            return;
        }
        _reloadTime -= deltaTime;
        if (_reloadTime > 0.0f)
        {
            return;
        }

        var needed = Math.Max(0, magazineSize - _ammo);
        var transferred = Math.Min(needed, _reserveAmmo);
        _ammo += transferred;
        _reserveAmmo -= transferred;
        _reloading = false;
        UpdateHud();
    }

    private void CancelReload()
    {
        if (!_reloading)
        {
            return;
        }
        _reloading = false;
        weaponAnimator?.SetTrigger("CancelReload");
        UpdateHud();
    }

    private void UpdateInteraction()
    {
        if (promptText is not null) promptText.Text = string.Empty;
        if (!Input.CursorLocked)
        {
            SetInteractionTarget(null);
            return;
        }

        var origin = camera?.GameObject.WorldPosition ?? GameObject.WorldPosition;
        var direction = camera?.GameObject.Forward ?? GameObject.Forward;
        if (!Physics.RaycastTagged(origin, direction, interactionDistance, interactableTag, GameObject, out var hit))
        {
            SetInteractionTarget(null);
            return;
        }

        SetInteractionTarget(hit.Entity);
        if (promptText is not null) promptText.Text = $"[E] {hit.Entity.Name}";
        if (Input.IsKeyPressed(KeyCode.E))
        {
            hit.Entity.TryInvoke(interactMethod, GameObject);
        }
    }

    private bool CheckGrounded()
    {
        var origin = GameObject.WorldPosition + Vector3.UnitY * groundProbeOffset;
        return Physics.Raycast(origin, -Vector3.UnitY, groundCheckDistance, GameObject, out var hit) &&
               hit.Normal.Y > 0.55f;
    }

    private void UpdateHud()
    {
        if (ammoText is not null)
        {
            ammoText.Text = _reloading ? $"RELOADING  {_ammo} / {_reserveAmmo}" : $"{_ammo} / {_reserveAmmo}";
        }
    }

    private void PublishStateChanges()
    {
        var ammoState = CurrentAmmoState();
        if (ammoState != _publishedAmmoState)
        {
            _publishedAmmoState = ammoState;
            AmmoChanged?.Invoke(ammoState);
        }

        var movementState = CurrentMovementState();
        if (movementState != _publishedMovementState)
        {
            _publishedMovementState = movementState;
            MovementStateChanged?.Invoke(movementState);
        }
    }

    private FpsAmmoState CurrentAmmoState()
    {
        return new FpsAmmoState(_ammo, _reserveAmmo, magazineSize, _reloading);
    }

    private FpsMovementState CurrentMovementState()
    {
        return new FpsMovementState(_grounded, _aiming, _sprinting, _crouching, _sliding);
    }

    private void SetInteractionTarget(GameObject? target)
    {
        var targetId = target?.EntityId ?? 0;
        if (targetId == _interactionTargetId)
        {
            return;
        }

        _interactionTargetId = targetId;
        InteractionTargetChanged?.Invoke(target);
    }

    private Vector3 ApplySpread(Vector3 direction, float degrees)
    {
        if (degrees <= 0.0f) return Vector3.Normalize(direction);
        var forward = Vector3.Normalize(direction);
        var right = Vector3.Cross(forward, Vector3.UnitY);
        if (right.LengthSquared() < 0.001f) right = Vector3.UnitX;
        else right = Vector3.Normalize(right);
        var up = Vector3.Normalize(Vector3.Cross(right, forward));
        var radius = MathF.Sqrt(NextFloat()) * MathF.Tan(degrees * MathF.PI / 180.0f);
        var angle = NextFloat() * MathF.PI * 2.0f;
        return Vector3.Normalize(forward + right * (MathF.Cos(angle) * radius) + up * (MathF.Sin(angle) * radius));
    }

    private float NextFloat()
    {
        _randomState ^= _randomState << 13;
        _randomState ^= _randomState >> 17;
        _randomState ^= _randomState << 5;
        return (_randomState & 0x00FFFFFFu) / 16777216.0f;
    }

    private static float Damp(float current, float target, float sharpness, float deltaTime)
    {
        return target + (current - target) * MathF.Exp(-MathF.Max(0.0f, sharpness) * deltaTime);
    }

    private static Vector3 MoveTowards(Vector3 current, Vector3 target, float maxDelta)
    {
        var difference = target - current;
        var distance = difference.Length();
        return distance <= maxDelta || distance < 0.00001f
            ? target
            : current + difference / distance * maxDelta;
    }

    private static Vector3 SmoothDamp(Vector3 current, Vector3 target, ref Vector3 velocity,
        float smoothTime, float deltaTime)
    {
        smoothTime = MathF.Max(0.0001f, smoothTime);
        var omega = 2.0f / smoothTime;
        var x = omega * deltaTime;
        var decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        var change = current - target;
        var temporary = (velocity + omega * change) * deltaTime;
        velocity = (velocity - omega * temporary) * decay;
        return target + (change + temporary) * decay;
    }
}
