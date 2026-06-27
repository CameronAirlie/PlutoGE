using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// A responsive, camera-relative third-person controller with an over-the-shoulder
/// orbit camera. Hold the right mouse button to aim/strafe and Shift to sprint.
/// </summary>
public sealed class ThirdPersonController : ScriptBehaviour
{
    [SerializedField] private CameraComponent? camera = null;

    [SerializedField] private float walkSpeed = 5.5f;
    [SerializedField] private float sprintSpeed = 8.0f;
    [SerializedField] private float acceleration = 24.0f;
    [SerializedField] private float deceleration = 30.0f;
    [SerializedField] private float turnSharpness = 14.0f;

    [SerializedField] private float jumpHeight = 1.35f;
    [SerializedField] private float gravity = -25.0f;
    [SerializedField] private float groundCheckDistance = 1.15f;
    [SerializedField] private float groundProbeOffset = 0.12f;
    [SerializedField] private float skinWidth = 0.03f;

    [SerializedField] private float mouseSensitivity = 0.12f;
    [SerializedField] private float cameraHeight = 1.55f;
    [SerializedField] private float cameraDistance = 4.25f;
    [SerializedField] private float shoulderOffset = 0.55f;
    [SerializedField] private float minimumPitch = -55.0f;
    [SerializedField] private float maximumPitch = 75.0f;
    [SerializedField] private float cameraCollisionPadding = 0.15f;

    private float _cameraYaw;
    private float _cameraPitch;
    private float _verticalVelocity;
    private float _currentSpeed;
    private bool _grounded;

    public override void OnCreate()
    {
        Input.CursorLocked = true;

        _cameraYaw = Rotation.Y + (camera?.GameObject.Rotation.Y ?? 0.0f);
        _cameraPitch = Math.Clamp(
            camera?.GameObject.Rotation.X ?? 12.0f,
            minimumPitch,
            maximumPitch);

        var rigidbody = GameObject.GetComponent<RigidbodyComponent>();
        if (rigidbody is not null)
        {
            rigidbody.IsKinematic = true;
            rigidbody.UseGravity = false;
            rigidbody.FreezeRotation = true;
        }

        var collider = GameObject.GetComponent<ColliderComponent>();
        if (collider is not null)
        {
            collider.Shape = ColliderShape.Capsule;
        }

        UpdateCameraPose();
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

        UpdateLook();
        UpdateMovement(deltaTime);
        UpdateCameraPose();
    }

    private void UpdateLook()
    {
        if (!Input.CursorLocked)
        {
            return;
        }

        var mouseDelta = Input.MouseDelta;
        _cameraYaw -= mouseDelta.X * mouseSensitivity;
        _cameraPitch = Math.Clamp(
            _cameraPitch - mouseDelta.Y * mouseSensitivity,
            minimumPitch,
            maximumPitch);

        ApplyCameraRotation();
    }

    private void UpdateMovement(float deltaTime)
    {
        _grounded = CheckGrounded();
        if (_grounded && _verticalVelocity < 0.0f)
        {
            _verticalVelocity = -2.0f;
        }

        if (_grounded && Input.IsKeyPressed(KeyCode.Space))
        {
            _verticalVelocity = MathF.Sqrt(jumpHeight * -2.0f * gravity);
            _grounded = false;
        }

        _verticalVelocity += gravity * deltaTime;

        var input = Vector2.Zero;
        if (Input.IsKeyDown(KeyCode.W)) input.Y += 1.0f;
        if (Input.IsKeyDown(KeyCode.S)) input.Y -= 1.0f;
        if (Input.IsKeyDown(KeyCode.D)) input.X += 1.0f;
        if (Input.IsKeyDown(KeyCode.A)) input.X -= 1.0f;

        if (input.LengthSquared() > 1.0f)
        {
            input = Vector2.Normalize(input);
        }

        var cameraForward = camera?.GameObject.Forward ?? GameObject.Forward;
        var cameraRight = camera?.GameObject.Right ?? GameObject.Right;
        cameraForward.Y = 0.0f;
        cameraRight.Y = 0.0f;

        if (cameraForward.LengthSquared() > 0.0001f) cameraForward = Vector3.Normalize(cameraForward);
        if (cameraRight.LengthSquared() > 0.0001f) cameraRight = Vector3.Normalize(cameraRight);

        var moveDirection = cameraRight * input.X + cameraForward * input.Y;
        if (moveDirection.LengthSquared() > 1.0f)
        {
            moveDirection = Vector3.Normalize(moveDirection);
        }

        var hasInput = moveDirection.LengthSquared() > 0.0001f;
        var sprinting = hasInput && Input.IsKeyDown(KeyCode.LeftShift);
        var targetSpeed = hasInput ? (sprinting ? sprintSpeed : walkSpeed) : 0.0f;
        var speedChange = targetSpeed > _currentSpeed ? acceleration : deceleration;
        _currentSpeed = MoveTowards(_currentSpeed, targetSpeed, speedChange * deltaTime);

        // Fortnite-style free movement: face travel normally, but face the camera
        // while aiming so backwards and sideways input become strafing.
        var aiming = Input.IsMouseButtonDown(MouseButton.Right);
        if (aiming || hasInput)
        {
            var desiredYaw = aiming
                ? _cameraYaw
                : MathF.Atan2(-moveDirection.X, -moveDirection.Z) * 180.0f / MathF.PI;
            var turnAmount = 1.0f - MathF.Exp(-turnSharpness * deltaTime);
            var newYaw = LerpAngle(Rotation.Y, desiredYaw, turnAmount);
            Rotation = new Vector3(0.0f, newYaw, 0.0f);
        }

        var horizontalVelocity = hasInput ? moveDirection * _currentSpeed : Vector3.Zero;
        var displacement = horizontalVelocity * deltaTime;
        displacement.Y = _verticalVelocity * deltaTime;

        var applied = Physics.MoveKinematic(GameObject, displacement, skinWidth);
        if (displacement.Y < 0.0f && MathF.Abs(applied.Y) < MathF.Abs(displacement.Y) * 0.5f)
        {
            _verticalVelocity = -2.0f;
        }
    }

    private void UpdateCameraPose()
    {
        if (camera is null)
        {
            return;
        }

        ApplyCameraRotation();

        var pivot = GameObject.WorldPosition + Vector3.UnitY * cameraHeight;
        var cameraForward = camera.GameObject.Forward;
        var cameraRight = camera.GameObject.Right;
        var desiredWorldPosition = pivot - cameraForward * cameraDistance + cameraRight * shoulderOffset;
        var cameraRay = desiredWorldPosition - pivot;
        var rayLength = cameraRay.Length();

        if (rayLength > 0.0001f &&
            Physics.Raycast(pivot, cameraRay / rayLength, rayLength, GameObject, out var hit))
        {
            var safeDistance = MathF.Max(0.05f, hit.Distance - cameraCollisionPadding);
            safeDistance = MathF.Min(safeDistance, rayLength);
            desiredWorldPosition = pivot + cameraRay / rayLength * safeDistance;
        }

        // The camera is expected to be a child of the controlled entity. Convert
        // the desired world offset back into that parent's yaw-only local space.
        var worldOffset = desiredWorldPosition - GameObject.WorldPosition;
        var playerRight = GameObject.Right;
        var playerForward = GameObject.Forward;
        camera.GameObject.Position = new Vector3(
            Vector3.Dot(worldOffset, playerRight),
            worldOffset.Y,
            -Vector3.Dot(worldOffset, playerForward));
    }

    private void ApplyCameraRotation()
    {
        if (camera is not null)
        {
            // The camera is parented to the player. The orientation we want is
            // world yaw followed by pitch (Ry * Rx), while PlutoGE stores local
            // Euler rotations as Rx * Ry * Rz. Convert that desired relative
            // orientation to XYZ Euler angles so turning the player cannot leak
            // pitch into camera roll.
            var degreesToRadians = MathF.PI / 180.0f;
            var radiansToDegrees = 180.0f / MathF.PI;
            var relativeYaw = DeltaAngle(Rotation.Y, _cameraYaw) * degreesToRadians;
            var pitch = _cameraPitch * degreesToRadians;

            var sinYaw = MathF.Sin(relativeYaw);
            var cosYaw = MathF.Cos(relativeYaw);
            var sinPitch = MathF.Sin(pitch);
            var cosPitch = MathF.Cos(pitch);

            var localPitch = MathF.Atan2(sinPitch, cosYaw * cosPitch);
            var localYaw = MathF.Asin(Math.Clamp(sinYaw * cosPitch, -1.0f, 1.0f));
            var localRoll = MathF.Atan2(-sinYaw * sinPitch, cosYaw);

            camera.GameObject.Rotation = new Vector3(
                localPitch * radiansToDegrees,
                localYaw * radiansToDegrees,
                localRoll * radiansToDegrees);
        }
    }

    private bool CheckGrounded()
    {
        var origin = GameObject.WorldPosition + Vector3.UnitY * groundProbeOffset;
        return Physics.Raycast(origin, -Vector3.UnitY, groundCheckDistance, GameObject, out var hit) &&
               hit.Normal.Y > 0.55f;
    }

    private static float MoveTowards(float current, float target, float maxDelta)
    {
        if (MathF.Abs(target - current) <= maxDelta)
        {
            return target;
        }

        return current + MathF.CopySign(maxDelta, target - current);
    }

    private static float LerpAngle(float from, float to, float amount)
    {
        return from + DeltaAngle(from, to) * Math.Clamp(amount, 0.0f, 1.0f);
    }

    private static float DeltaAngle(float from, float to)
    {
        var delta = (to - from) % 360.0f;
        if (delta > 180.0f) delta -= 360.0f;
        if (delta < -180.0f) delta += 360.0f;
        return delta;
    }
}
