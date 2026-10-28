using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// Team-coloured burst and light flash where a goal is scored. The entity holds
/// the particle system; its child <c>Flash</c> holds a point light.
/// </summary>
public sealed class GoalExplosion : ScriptBehaviour
{
    [SerializedField] private int particleCount = 260;
    [SerializedField] private float flashIntensity = 260.0f;
    [SerializedField] private float flashDuration = 1.2f;

    private ParticleSystemComponent? _particles;
    private LightComponent? _flash;
    private float _flashTimer;

    public override void OnCreate()
    {
        _particles = GameObject.GetComponent<ParticleSystemComponent>();
        _flash = GameObject.FindChild("Flash")?.GetComponent<LightComponent>();
        _particles?.Stop();
        if (_flash is not null) _flash.Intensity = 0.0f;
    }

    public void Play(Vector3 worldPosition, Team scoringTeam)
    {
        var colour = scoringTeam == Team.Blue ? new Vector3(0.25f, 0.6f, 1.0f) : new Vector3(1.0f, 0.5f, 0.1f);
        GameObject.WorldPosition = worldPosition;
        if (_particles is not null)
        {
            _particles.StartColor = colour;
            _particles.Emit(worldPosition, Math.Max(particleCount, 1));
        }
        if (_flash is not null) _flash.Color = colour;
        _flashTimer = MathF.Max(flashDuration, 0.01f);
    }

    public override void OnUpdate(float deltaTime)
    {
        if (_flash is null || _flashTimer <= 0.0f) return;
        _flashTimer = MathF.Max(0.0f, _flashTimer - deltaTime);
        var t = _flashTimer / MathF.Max(flashDuration, 0.01f);
        _flash.Intensity = flashIntensity * t * t;
    }
}
