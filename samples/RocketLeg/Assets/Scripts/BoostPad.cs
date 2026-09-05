using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>Shared floor pickup. Dim pads recharge after six seconds of play.</summary>
public sealed class BoostPad : ScriptBehaviour
{
    [SerializedField] private float amount = 35.0f;
    [SerializedField] private float rechargeSeconds = 6.0f;
    private GameObject? _blue;
    private GameObject? _orange;
    private MeshComponent? _mesh;
    private float _cooldown;

    public override void OnCreate()
    {
        _blue = GameObject.Find("Blue Car");
        _orange = GameObject.Find("Orange Car");
        _mesh = GameObject.GetComponent<MeshComponent>();
        ResetPad();
    }

    public void ResetPad()
    {
        _cooldown = 0.0f;
        ShowReady(true);
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_blue?.GetComponent<ArcadeCarController>()?.IsFrozen != false) return;
        if (_cooldown > 0.0f)
        {
            _cooldown = MathF.Max(0.0f, _cooldown - fixedDeltaTime);
            if (_cooldown <= 0.0f) ShowReady(true);
            return;
        }
        if (Collect(_blue) || Collect(_orange))
        {
            _cooldown = MathF.Max(rechargeSeconds, 0.1f);
            ShowReady(false);
        }
    }

    private bool Collect(GameObject? car)
    {
        if (car is null) return false;
        var offset = car.WorldPosition - GameObject.WorldPosition;
        return MathF.Abs(offset.Y) < 1.6f && offset.X * offset.X + offset.Z * offset.Z < 5.76f &&
               car.GetComponent<ArcadeCarController>()?.CollectBoost(amount) == true;
    }

    private void ShowReady(bool ready)
    {
        if (_mesh is null) return;
        _mesh.Color = ready ? new Vector3(1.0f, 0.65f, 0.08f) : new Vector3(0.12f, 0.1f, 0.06f);
        _mesh.Emission = ready ? new Vector3(1.5f, 0.65f, 0.04f) : Vector3.Zero;
    }
}
