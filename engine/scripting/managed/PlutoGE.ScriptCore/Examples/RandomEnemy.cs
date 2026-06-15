using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

public sealed class RandomEnemy : ScriptBehaviour
{
    [SerializedField] private float moveSpeed = 1.8f;
    [SerializedField] private float turnIntervalMin = 0.8f;
    [SerializedField] private float turnIntervalMax = 2.4f;
    [SerializedField] private float flashDuration = 0.25f;
    [SerializedField] private string hitByTag = "Player";

    private MeshComponent? _mesh;
    private Vector3 _baseColor = Vector3.One;
    private Vector3 _direction = Vector3.UnitZ;
    private float _nextTurnTime;
    private float _flashUntil;
    private float _time;
    private uint _randomState;

    public override void OnCreate()
    {
        _mesh = GameObject.GetComponent<MeshComponent>();
        if (_mesh is not null)
        {
            _baseColor = _mesh.Color;
        }

        _randomState = EntityId * 747796405u + 2891336453u;
        PickNewDirection();
    }

    public override void OnUpdate(float deltaTime)
    {
        _time += deltaTime;
        if (_time >= _nextTurnTime)
        {
            PickNewDirection();
        }

        if (_direction.LengthSquared() > 0.001f)
        {
            var displacement = _direction * moveSpeed * deltaTime;
            Physics.MoveKinematic(GameObject, displacement);

            var yaw = MathF.Atan2(_direction.X, -_direction.Z) * 180.0f / MathF.PI;
            Rotation = new Vector3(Rotation.X, yaw, Rotation.Z);
        }

        if (_mesh is not null && _flashUntil > 0.0f && _time >= _flashUntil)
        {
            _mesh.Color = _baseColor;
            _flashUntil = 0.0f;
        }
    }

    public override void OnCollisionEnter(GameObject other)
    {
        if (!string.IsNullOrEmpty(hitByTag) && !other.HasTag(hitByTag))
        {
            return;
        }

        TakeDamage();
    }

    public void TakeDamage()
    {
        FlashRed();
    }

    public void TakeDame()
    {
        TakeDamage();
    }

    public void FlashRed()
    {
        if (_mesh is null)
        {
            return;
        }

        _mesh.Color = new Vector3(1.0f, 0.05f, 0.03f);
        _flashUntil = _time + flashDuration;
    }

    private void PickNewDirection()
    {
        var angle = NextRandom01() * MathF.Tau;
        _direction = Vector3.Normalize(new Vector3(MathF.Sin(angle), 0.0f, MathF.Cos(angle)));
        _nextTurnTime = _time + Lerp(turnIntervalMin, MathF.Max(turnIntervalMin, turnIntervalMax), NextRandom01());
    }

    private float NextRandom01()
    {
        _randomState = _randomState * 1664525u + 1013904223u;
        return (_randomState & 0x00ffffffu) / 16777216.0f;
    }

    private static float Lerp(float a, float b, float t)
    {
        return a + (b - a) * Math.Clamp(t, 0.0f, 1.0f);
    }
}
