using System.Numerics;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>Attach to a moving character with a Sound Emitter and optional Particle System.</summary>
public sealed class SurfaceFootsteps : ScriptBehaviour
{
    [SerializedField] private float stride = 1.5f;
    [SerializedField] private float groundDistance = 1.5f;
    private Vector3 previous;
    private float distance;

    public override void OnCreate()
    {
        previous = GameObject.WorldPosition;
        if (GameObject.GetComponent<ParticleSystemComponent>() is { } particles)
        {
            particles.PlayOnAwake = false;
            particles.Stop();
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (deltaTime <= 0) return;
        var position = GameObject.WorldPosition;
        var movement = position - previous;
        previous = position;
        if (!Physics.Raycast(position, -Vector3.UnitY, MathF.Max(0.01f, groundDistance), GameObject, out var hit))
        {
            distance = 0;
            return;
        }
        distance += new Vector2(movement.X, movement.Z).Length();
        if (distance < MathF.Max(0.1f, stride)) return;
        distance = 0;
        var response = SurfaceResponses.Resolve(hit, SurfaceEvent.Footstep);
        if (response.Sound.Length > 0 && GameObject.GetComponent<SoundEmitterComponent>() is { } sound)
        {
            sound.Clip = response.Sound;
            sound.PlayOneShot();
        }
        if (response.Particles.Length > 0 && GameObject.GetComponent<ParticleSystemComponent>() is { } particles)
        {
            particles.ParticleSystemAsset = response.Particles;
            particles.Looping = false;
            particles.EmissionRateOverTime = 0;
            particles.EmitAt(hit.Point, 8);
        }
        if (response.DecalMaterial.Length > 0)
            Decals.Spawn(hit, response.DecalMaterial, new Vector2(0.15f, 0.3f), lifetime: 8, fadeDuration: 2);
    }
}
