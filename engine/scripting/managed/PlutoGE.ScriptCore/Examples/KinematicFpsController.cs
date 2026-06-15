using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

public sealed class KinematicFpsController : ScriptBehaviour
{
    [SerializedField] private CameraComponent? camera = null;
    [SerializedField] private float moveSpeed = 6.0f;
    [SerializedField] private float sprintMultiplier = 1.5f;
    [SerializedField] private float mouseSensitivity = 0.12f;
    [SerializedField] private float jumpHeight = 1.25f;
    [SerializedField] private float gravity = -24.0f;
    [SerializedField] private float groundCheckDistance = 1.15f;
    [SerializedField] private float groundProbeOffset = 0.12f;
    [SerializedField] private float skinWidth = 0.03f;
    [SerializedField] private float shootDistance = 100.0f;
    [SerializedField] private float fireCooldown = 0.12f;
    [SerializedField] private string damageableTag = "Enemy";
    [SerializedField] private string damageMethod = "TakeDamage";

    private float _yaw;
    private float _pitch;
    private float _verticalVelocity;
    private float _nextFireTime;
    private float _time;
    private bool _grounded;

    public override void OnCreate()
    {
        Input.CursorLocked = true;
        _yaw = Rotation.Y;
        _pitch = camera is null ? 0.0f : camera.GameObject.Rotation.X;

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
    }

    public override void OnUpdate(float deltaTime)
    {
        _time += deltaTime;

        if (Input.IsKeyPressed(KeyCode.Escape))
        {
            Input.CursorLocked = !Input.CursorLocked;
        }

        Look();
        Move(deltaTime);
        Shoot();
    }

    private void Look()
    {
        if (!Input.CursorLocked)
        {
            return;
        }

        var mouseDelta = Input.MouseDelta;
        _yaw -= mouseDelta.X * mouseSensitivity;
        _pitch = Math.Clamp(_pitch - mouseDelta.Y * mouseSensitivity, -89.0f, 89.0f);

        Rotation = new Vector3(0.0f, _yaw, 0.0f);
        if (camera is not null)
        {
            camera.GameObject.Rotation = new Vector3(_pitch, 0.0f, 0.0f);
        }
    }

    private void Move(float deltaTime)
    {
        _grounded = CheckGrounded();
        if (_grounded && _verticalVelocity < 0.0f)
        {
            _verticalVelocity = -2.0f;
        }

        if (_grounded && Input.IsKeyPressed(KeyCode.Space))
        {
            _verticalVelocity = MathF.Sqrt(jumpHeight * -2.0f * gravity);
        }

        _verticalVelocity += gravity * deltaTime;

        var input = Vector3.Zero;
        if (Input.IsKeyDown(KeyCode.W)) input.Z += 1.0f;
        if (Input.IsKeyDown(KeyCode.S)) input.Z -= 1.0f;
        if (Input.IsKeyDown(KeyCode.D)) input.X += 1.0f;
        if (Input.IsKeyDown(KeyCode.A)) input.X -= 1.0f;

        if (input.LengthSquared() > 1.0f)
        {
            input = Vector3.Normalize(input);
        }

        var rotationRadians = MathF.PI / 180.0f * _yaw;
        var forward = GameObject.Forward; //  new Vector3(MathF.Sin(rotationRadians), 0.0f, -MathF.Cos(rotationRadians));
        var right = GameObject.Right; //  new Vector3(MathF.Cos(rotationRadians), 0.0f, MathF.Sin(rotationRadians));
        var speed = moveSpeed * (Input.IsKeyDown(KeyCode.LeftShift) ? sprintMultiplier : 1.0f);

        var horizontalVelocity = (right * input.X + forward * input.Z) * speed;
        var displacement = horizontalVelocity * deltaTime + new Vector3(0.0f, _verticalVelocity * deltaTime, 0.0f);
        var applied = Physics.MoveKinematic(GameObject, displacement, skinWidth);

        if (displacement.Y < 0.0f && MathF.Abs(applied.Y) < MathF.Abs(displacement.Y) * 0.5f)
        {
            _verticalVelocity = -2.0f;
        }
    }

    private bool CheckGrounded()
    {
        var origin = GameObject.WorldPosition + new Vector3(0.0f, groundProbeOffset, 0.0f);
        return Physics.Raycast(origin, -Vector3.UnitY, groundCheckDistance, GameObject, out var hit) &&
               hit.Normal.Y > 0.55f;
    }

    private void Shoot()
    {
        if (!Input.CursorLocked ||
            !Input.IsMouseButtonDown(MouseButton.Left) ||
            _time < _nextFireTime)
        {
            return;
        }

        _nextFireTime = _time + fireCooldown;

        var origin = camera is not null ? camera.GameObject.WorldPosition : GameObject.WorldPosition;
        var direction = camera is not null ? camera.GameObject.Forward : GameObject.Forward;
        if (Physics.RaycastTagged(origin, direction, shootDistance, damageableTag, GameObject, out var hit))
        {
            if (!hit.Entity.TryInvoke(damageMethod))
            {
                Debug.Log($"Hit entity {hit.Entity.EntityId}, but it has no {damageMethod}() method.");
                return;
            }

            Debug.Log($"FPS hit {damageableTag} entity {hit.Entity.EntityId} at {hit.Distance:0.00}m");
        }
    }
}
