using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// Presentation for an <see cref="ArcadeCarController"/> on the same entity:
/// suspension-following, steering and spinning wheels, the team-coloured boost
/// flame, the supersonic trail and the demolition burst. It only reads car
/// state, so the physics controller stays free of rendering concerns.
/// <para>
/// Expected children (created by <c>generate_arena.py</c>): <c>Visual Root</c>,
/// which cancels the chassis scale so everything under it is in metres, holding
/// <c>Wheel FL/FR/RL/RR</c>, <c>Exhaust</c>, <c>Boost Flame</c> and
/// <c>Supersonic Trail</c>. The demolition emitter is a scene-level entity.
/// </para>
/// </summary>
public sealed class CarVisuals : ScriptBehaviour
{
    private static readonly string[] WheelNames = { "Wheel FL", "Wheel FR", "Wheel RL", "Wheel RR" };

    [SerializedField] private float wheelRadius = 0.32f;
    // Highest a compressed wheel may ride, in chassis-height units of travel.
    [SerializedField] private float minimumWheelExtension = 0.35f;
    [SerializedField] private GameObject? demolitionEffect;

    private ArcadeCarController? _car;
    private RigidbodyComponent? _body;
    private GameObject? _visualRoot;
    private readonly GameObject?[] _wheels = new GameObject?[ArcadeCarController.WheelCount];
    private readonly float[] _wheelSpin = new float[ArcadeCarController.WheelCount];
    private MeshComponent? _exhaust;
    private ParticleSystemComponent? _boostFlame;
    private ParticleSystemComponent? _supersonicTrail;
    private ParticleSystemComponent? _demolitionBurst;
    private Vector3 _exhaustIdleEmission;
    private Vector3 _flameColour;
    private bool _flameOn;
    private bool _trailOn;
    private bool _wreckHidden;
    private Vector3 _lastDrivenPosition;

    public override void OnCreate()
    {
        _car = GameObject.GetComponent<ArcadeCarController>();
        _body = GameObject.GetComponent<RigidbodyComponent>();
        if (_car is null)
        {
            Debug.LogError($"{GameObject.Name}: CarVisuals needs an ArcadeCarController on the same entity.");
            return;
        }

        _visualRoot = GameObject.FindChild("Visual Root");
        for (var wheel = 0; wheel < _wheels.Length; wheel++)
            _wheels[wheel] = _visualRoot?.FindChild(WheelNames[wheel]);
        _exhaust = _visualRoot?.FindChild("Exhaust")?.GetComponent<MeshComponent>();
        _boostFlame = _visualRoot?.FindChild("Boost Flame")?.GetComponent<ParticleSystemComponent>();
        _supersonicTrail = _visualRoot?.FindChild("Supersonic Trail")?.GetComponent<ParticleSystemComponent>();
        demolitionEffect ??= GameObject.Find("Demolition Effect");
        _demolitionBurst = demolitionEffect?.GetComponent<ParticleSystemComponent>();

        _flameColour = _car.Team == Team.Blue ? new Vector3(0.35f, 0.75f, 1.0f) : new Vector3(1.0f, 0.55f, 0.12f);
        if (_boostFlame is not null) _boostFlame.StartColor = _flameColour;
        if (_exhaust is not null) _exhaustIdleEmission = _exhaust.Emission;
        _boostFlame?.Stop();
        _supersonicTrail?.Stop();
        _car.Demolished += OnDemolished;
        _lastDrivenPosition = GameObject.WorldPosition;
    }

    public override void OnDestroy()
    {
        if (_car is not null) _car.Demolished -= OnDemolished;
    }

    public override void OnLateUpdate(float deltaTime)
    {
        if (_car is null) return;

        var wrecked = _car.IsDemolished;
        if (wrecked != _wreckHidden)
        {
            _wreckHidden = wrecked;
            if (_visualRoot is not null) _visualRoot.Active = !wrecked;
        }
        if (wrecked) return;
        _lastDrivenPosition = GameObject.WorldPosition;

        UpdateWheels(deltaTime);
        SetFlame(_car.IsBoosting);
        SetTrail(_car.IsSupersonic);
    }

    private void UpdateWheels(float deltaTime)
    {
        // Wheels follow suspension travel without adding snagging wheel
        // colliders. The chassis remains the sole body for ball/car impacts.
        var chassisHeight = MathF.Max(MathF.Abs(GameObject.Scale.Y), 0.2f);
        var forwardSpeed = _body is null ? 0.0f : Vector3.Dot(_body.Velocity, GameObject.Forward);
        for (var wheel = 0; wheel < _wheels.Length; wheel++)
        {
            var visual = _wheels[wheel];
            if (visual is null) continue;
            var extension = MathF.Max(_car!.GetWheelExtension(wheel), minimumWheelExtension);
            var position = visual.Position;
            position.Y = wheelRadius - extension * chassisHeight;
            visual.Position = position;

            // Free wheels keep spinning down slowly rather than stopping dead.
            var spinRate = _car.IsWheelTouching(wheel) ? forwardSpeed / MathF.Max(wheelRadius, 0.05f) : 0.0f;
            _wheelSpin[wheel] = (_wheelSpin[wheel] - spinRate * deltaTime) % (MathF.PI * 2.0f);
            var steer = wheel < 2 ? -_car.SteerAngle : 0.0f;
            // The cylinder's axis is local Y: lay it along the axle (X), then spin about the axle.
            visual.RotationQuaternion = Quaternion.CreateFromAxisAngle(Vector3.UnitY, steer) *
                                        Quaternion.CreateFromAxisAngle(Vector3.UnitX, _wheelSpin[wheel]) *
                                        Quaternion.CreateFromAxisAngle(Vector3.UnitZ, MathF.PI * 0.5f);
        }
    }

    private void SetFlame(bool on)
    {
        if (_flameOn == on) return;
        _flameOn = on;
        if (on) _boostFlame?.Play();
        else _boostFlame?.Stop(clear: false);
        if (_exhaust is not null) _exhaust.Emission = on ? _flameColour * 6.0f : _exhaustIdleEmission;
    }

    private void SetTrail(bool on)
    {
        if (_trailOn == on) return;
        _trailOn = on;
        if (on) _supersonicTrail?.Play();
        else _supersonicTrail?.Stop(clear: false);
    }

    private void OnDemolished(ArcadeCarController attacker)
    {
        SetFlame(false);
        SetTrail(false);
        if (_demolitionBurst is null) return;
        _demolitionBurst.StartColor = _flameColour;
        _demolitionBurst.Emit(_lastDrivenPosition, 90);
    }
}
