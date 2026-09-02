using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>Keeps the match ball lively, bounded, and recoverable.</summary>
public sealed class RocketBall : ScriptBehaviour
{
    [SerializedField] private float maximumSpeed = 30.0f;
    [SerializedField] private float lostBallHeight = -5.0f;

    private RigidbodyComponent? _body;
    private Vector3 _spawnPosition;

    public override void OnCreate()
    {
        _spawnPosition = GameObject.WorldPosition;
        _body = GameObject.GetComponent<RigidbodyComponent>();
        if (_body is null)
        {
            Debug.LogError("RocketBall needs a RigidbodyComponent.");
            return;
        }

        _body.Mass = 18.0f;
        _body.LinearDrag = 0.05f;
        _body.AngularDrag = 0.05f;
        _body.Friction = 0.45f;
        _body.UseGravity = true;
        _body.IsKinematic = false;
        _body.FreezeRotation = false;
    }

    public override void OnFixedUpdate(float fixedDeltaTime)
    {
        if (_body is null || _body.IsKinematic) return;

        var speed = _body.Velocity.Length();
        if (speed > maximumSpeed)
        {
            _body.Velocity = _body.Velocity / speed * maximumSpeed;
        }

        if (GameObject.WorldPosition.Y < lostBallHeight)
        {
            ResetBall();
        }
    }

    public void ResetBall()
    {
        GameObject.WorldPosition = _spawnPosition;
        GameObject.WorldRotation = Vector3.Zero;
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
}
