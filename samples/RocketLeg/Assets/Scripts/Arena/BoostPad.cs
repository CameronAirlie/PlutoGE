using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// Shared floor pickup. Big pads fill the tank and have a floating orb; small
/// pads top it up. A collected pad dims and recharges during active play. Cars
/// are found by their "car" tag, and a full car leaves the pad for others.
/// </summary>
public sealed class BoostPad : ScriptBehaviour
{
    [SerializedField] private bool big = false;
    [SerializedField] private float smallAmount = 12.0f;
    [SerializedField] private float bigAmount = 100.0f;
    [SerializedField] private float smallRechargeSeconds = 4.0f;
    [SerializedField] private float bigRechargeSeconds = 10.0f;
    [SerializedField] private float smallPickupRadius = 1.3f;
    [SerializedField] private float bigPickupRadius = 2.1f;
    [SerializedField] private float pickupHeight = 1.6f;

    private static readonly Vector3 ReadyColour = new(1.0f, 0.62f, 0.08f);
    private static readonly Vector3 ReadyEmission = new(2.4f, 1.05f, 0.08f);
    private static readonly Vector3 SpentColour = new(0.12f, 0.1f, 0.06f);

    private ArcadeCarController[] _cars = Array.Empty<ArcadeCarController>();
    private MeshComponent? _mesh;
    private GameObject? _orb;
    private MeshComponent? _orbMesh;
    private Vector3 _orbBase;
    private float _cooldown;
    private float _time;

    public bool IsBig => big;
    public bool IsReady => _cooldown <= 0.0f;

    public override void OnCreate()
    {
        _mesh = GameObject.GetComponent<MeshComponent>();
        _orb = GameObject.FindChild("Orb");
        _orbMesh = _orb?.GetComponent<MeshComponent>();
        if (_orb is not null) _orbBase = _orb.WorldPosition;
        ResetPad();
    }

    public void ResetPad()
    {
        _cooldown = 0.0f;
        ShowReady(true);
    }

    public override void OnUpdate(float deltaTime)
    {
        if (_orb is null || !IsReady) return;
        _time += deltaTime;
        // World space: the pad's flat scale would squash a local offset.
        _orb.WorldPosition = _orbBase + Vector3.UnitY * (MathF.Sin(_time * 2.2f) * 0.12f);
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_cars.Length == 0) _cars = FindCars();
        if (!AnyCarInPlay()) return;
        if (_cooldown > 0.0f)
        {
            _cooldown = MathF.Max(0.0f, _cooldown - fixedDeltaTime);
            if (_cooldown <= 0.0f) ShowReady(true);
            return;
        }

        foreach (var car in _cars)
        {
            if (!Collect(car)) continue;
            _cooldown = MathF.Max(big ? bigRechargeSeconds : smallRechargeSeconds, 0.1f);
            ShowReady(false);
            return;
        }
    }

    // Scripts start in scene order and the generated pads come before the cars,
    // so the car scripts may not exist yet in OnCreate; look them up on first use.
    private static ArcadeCarController[] FindCars()
    {
        var cars = GameObject.FindByTag("car");
        var controllers = new ArcadeCarController[cars.Length];
        var count = 0;
        foreach (var car in cars)
            if (car.GetComponent<ArcadeCarController>() is { } controller)
                controllers[count++] = controller;
        Array.Resize(ref controllers, count);
        return controllers;
    }

    // Pads only recharge while play is live, not during kickoff freezes.
    private bool AnyCarInPlay()
    {
        foreach (var car in _cars)
            if (!car.IsFrozen) return true;
        return false;
    }

    private bool Collect(ArcadeCarController car)
    {
        if (car.IsFrozen) return false;
        var radius = big ? bigPickupRadius : smallPickupRadius;
        var offset = car.Position - GameObject.WorldPosition;
        return MathF.Abs(offset.Y) < pickupHeight && offset.X * offset.X + offset.Z * offset.Z < radius * radius &&
               car.CollectBoost(big ? bigAmount : smallAmount);
    }

    private void ShowReady(bool ready)
    {
        if (_mesh is not null)
        {
            _mesh.Color = ready ? ReadyColour : SpentColour;
            _mesh.Emission = ready ? ReadyEmission * (big ? 1.0f : 0.7f) : Vector3.Zero;
        }
        if (_orb is not null) _orb.Active = ready;
        if (_orbMesh is not null) _orbMesh.Emission = ReadyEmission * 1.6f;
    }
}
