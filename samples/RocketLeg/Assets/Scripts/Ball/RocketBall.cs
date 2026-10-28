using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// The match ball: lively, bounded and recoverable. Car touches get an extra
/// "hit assist" impulse on top of the rigid-body response, so a hit's power
/// depends on how fast the car was closing rather than on the mass ratio, and
/// the last toucher is tracked for goal credit.
/// </summary>
public sealed class RocketBall : ScriptBehaviour
{
    [SerializedField] private float maximumSpeed = 65.0f;
    [SerializedField] private float maximumAngularSpeed = 9.0f;
    [SerializedField] private float lostBallHeight = -5.0f;
    // Extra ball speed per unit of closing speed, so a 20 m/s hit adds 20 * hitAssist.
    [SerializedField] private float hitAssist = 0.45f;
    // The assist leans the push this much towards the car's travel direction,
    // so hits go where the car is heading instead of along the contact normal.
    [SerializedField] private float hitAimBlend = 0.35f;
    // Vertical share of the assist is scaled by this, keeping ground hits low.
    [SerializedField] private float hitVerticalScale = 0.6f;
    [SerializedField] private float minimumHitSpeed = 2.0f;
    [SerializedField] private float touchCooldown = 0.1f;

    private RigidbodyComponent? _body;
    private Vector3 _spawnPosition;
    private Vector3 _lastVelocity;
    private float _touchCooldownTimer;
    private float _colliderRadius = 0.5f;

    public Vector3 Position => GameObject.WorldPosition;
    public float Radius => MathF.Abs(GameObject.Scale.X) * _colliderRadius;
    public Vector3 Velocity => _body?.Velocity ?? Vector3.Zero;
    public ArcadeCarController? LastTouch { get; private set; }
    public bool IsFrozen => _body is null || _body.IsKinematic;

    /// <summary>Raised for every car touch, with the closing speed of the hit.</summary>
    public event Action<ArcadeCarController, float>? Touched;

    public override void OnCreate()
    {
        _spawnPosition = GameObject.WorldPosition;
        _body = GameObject.GetComponent<RigidbodyComponent>();
        if (_body is null)
        {
            Debug.LogError("RocketBall needs a RigidbodyComponent.");
            return;
        }

        // Mass, drag, bounce and the sphere collider are authored on the
        // components and used as they are; only the play state is set here.
        if (GameObject.GetComponent<ColliderComponent>() is { Shape: ColliderShape.Sphere } collider)
            _colliderRadius = MathF.Max(collider.Radius, 0.01f);
        _body.UseGravity = true;
        _body.IsKinematic = false;
        _body.FreezeRotation = false;
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        _touchCooldownTimer = MathF.Max(0.0f, _touchCooldownTimer - fixedDeltaTime);
        if (_body is null || _body.IsKinematic) return;

        // A very fast ball can slip through the thin stadium mesh; put it back.
        if (ArenaLayout.TryContain(GameObject.WorldPosition, Radius, out var inside, out var inward))
        {
            GameObject.WorldPosition = inside;
            var outward = Vector3.Dot(_body.Velocity, inward);
            if (outward < 0.0f) _body.Velocity -= inward * outward;
        }

        _body.AddForce(-Vector3.UnitY * (Gravity.Extra * _body.Mass));
        _lastVelocity = ClampMagnitude(_body.Velocity, maximumSpeed);
        _body.Velocity = _lastVelocity;
        _body.AngularVelocity = ClampMagnitude(_body.AngularVelocity, maximumAngularSpeed);

        if (GameObject.WorldPosition.Y < lostBallHeight)
        {
            ResetBall();
        }
    }

    public override void OnCollisionEnter(GameObject other)
    {
        if (_body is null || _body.IsKinematic || !other.HasTag("car")) return;
        var car = other.GetComponent<ArcadeCarController>();
        if (car is null || car.IsDemolished) return;

        var contactDirection = SafeDirection(GameObject.WorldPosition - other.WorldPosition, other.Forward);
        var carVelocity = car.PreContactVelocity;
        var closingSpeed = Vector3.Dot(carVelocity - _lastVelocity, contactDirection);
        var sameCarTouch = LastTouch is not null && LastTouch.EntityId == car.EntityId && _touchCooldownTimer > 0.0f;
        LastTouch = car;
        _touchCooldownTimer = touchCooldown;
        if (sameCarTouch) return;
        Touched?.Invoke(car, MathF.Max(closingSpeed, 0.0f));
        if (closingSpeed < minimumHitSpeed) return;

        var aim = SafeDirection(contactDirection * (1.0f - hitAimBlend) +
                                SafeDirection(carVelocity, contactDirection) * hitAimBlend, contactDirection);
        aim.Y *= hitVerticalScale;
        aim = SafeDirection(aim, contactDirection);
        _body.AddImpulse(aim * (closingSpeed * hitAssist * _body.Mass));
    }

    public void ResetBall() => ResetBall(_spawnPosition);

    public void ResetBall(Vector3 worldPosition)
    {
        GameObject.WorldPosition = worldPosition;
        GameObject.WorldRotation = Vector3.Zero;
        LastTouch = null;
        _lastVelocity = Vector3.Zero;
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

    private static Vector3 SafeDirection(Vector3 value, Vector3 fallback) =>
        value.LengthSquared() > 0.0001f ? Vector3.Normalize(value) : fallback;

    private static Vector3 ClampMagnitude(Vector3 value, float maximumLength)
    {
        var lengthSquared = value.LengthSquared();
        if (lengthSquared <= maximumLength * maximumLength) return value;
        return Vector3.Normalize(value) * maximumLength;
    }
}
