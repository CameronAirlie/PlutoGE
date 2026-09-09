using System;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Plays a particle system for grounded wheelspin and traction-loss skids.
/// </summary>
public sealed class TyreSmokeController : ScriptBehaviour
{
    [SerializedField] private GameObject? vehicle = null;
    [SerializedField] private GameObject? particleObject = null;
    [SerializedField] private float wheelSpinSpeedThreshold = 2.0f;
    [SerializedField] private float minimumSmokeAmount = 0.08f;
    [SerializedField] private float emissionRateAtFullSmoke = 80.0f;
    [SerializedField] private bool loopWhileSmoking = true;
    [SerializedField] private bool clearWhenStopped = false;

    private ParticleSystemComponent? _particles;
    private float _baseEmissionRate;

    public override void OnCreate()
    {
        _particles = (particleObject ?? GameObject).GetComponent<ParticleSystemComponent>();
        if (_particles is null)
        {
            return;
        }

        _baseEmissionRate = _particles.EmissionRateOverTime;
        _particles.Looping = loopWhileSmoking;
        _particles.EmissionRateOverTime = 0.0f;
        if (_particles.Playing)
        {
            _particles.Stop(clearWhenStopped);
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (_particles is null)
        {
            return;
        }

        var targetVehicle = vehicle ?? GameObject;
        if (!RaycastVehicleController.TryGetTelemetry(targetVehicle, out var telemetry))
        {
            StopSmoke();
            return;
        }

        var wheelSpinSpeed = MathF.Abs(telemetry.DrivenWheelSpeed);
        var vehicleSpeed = MathF.Abs(telemetry.ForwardSpeed);
        var spinningFasterThanMoving = wheelSpinSpeed > vehicleSpeed + MathF.Max(wheelSpinSpeedThreshold, 0.0f);
        var planarSpeed = MathF.Sqrt(telemetry.ForwardSpeed * telemetry.ForwardSpeed +
                                    telemetry.LateralSpeed * telemetry.LateralSpeed);
        var skidSmoke = Math.Clamp((telemetry.LateralSlip - 0.25f) / 0.75f, 0.0f, 1.0f) *
                        Math.Clamp((planarSpeed - 1.0f) / 3.0f, 0.0f, 1.0f);
        if (telemetry.Handbrake || telemetry.Brake >= 0.95f)
        {
            skidSmoke = MathF.Max(skidSmoke, Math.Clamp((planarSpeed - 2.0f) / 10.0f, 0.0f, 1.0f));
        }
        var smoke01 = MathF.Max(skidSmoke,
            spinningFasterThanMoving ? Math.Clamp(telemetry.TyreSmoke, 0.0f, 1.0f) : 0.0f);
        var shouldSmoke = telemetry.IsGrounded &&
                          smoke01 > 0.0f &&
                          smoke01 >= MathF.Max(minimumSmokeAmount, 0.0f);

        if (!shouldSmoke)
        {
            StopSmoke();
            return;
        }

        _particles.EmissionRateOverTime = MathF.Max(_baseEmissionRate, emissionRateAtFullSmoke) * smoke01;
        if (!_particles.Playing)
        {
            _particles.Play();
        }
    }

    private void StopSmoke()
    {
        if (_particles is null)
        {
            return;
        }

        _particles.EmissionRateOverTime = 0.0f;
        if (_particles.Playing)
        {
            _particles.Stop(clearWhenStopped);
        }
    }
}
