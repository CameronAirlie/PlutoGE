using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// A ring projected straight down from the ball onto the floor, so players can
/// judge where an airborne ball will land. It shrinks and fades with height.
/// </summary>
public sealed class BallMarker : ScriptBehaviour
{
    [SerializedField] private GameObject? ball;
    [SerializedField] private float maximumHeight = ArenaLayout.CeilingHeight;
    [SerializedField] private float surfaceOffset = 0.02f;

    private RocketBall? _ball;
    private MeshComponent? _mesh;
    private Vector3 _baseScale;
    private Vector3 _baseEmission;

    public override void OnCreate()
    {
        ball ??= GameObject.Find("Ball");
        _mesh = GameObject.GetComponent<MeshComponent>();
        _baseScale = GameObject.Scale;
        if (_mesh is not null) _baseEmission = _mesh.Emission;
    }

    public override void OnLateUpdate(float deltaTime)
    {
        // The marker starts before the ball in scene order, so the ball's
        // script may not exist during OnCreate; look it up on first use.
        _ball ??= ball?.GetComponent<RocketBall>();
        if (ball is null || _ball is null) return;
        var centre = ball.WorldPosition;
        // Ignore the ball itself; only the floor or goal floor below counts.
        var hasFloor = Physics.RaycastTagged(centre, -Vector3.UnitY, maximumHeight + _ball.Radius, "ground", ball, out var hit);
        var height = hasFloor ? hit.Distance - _ball.Radius : maximumHeight;
        var visible = hasFloor && height > 0.4f && ball.Active;
        if (GameObject.Active != visible) GameObject.Active = visible;
        if (!visible) return;

        var closeness = 1.0f - Math.Clamp(height / maximumHeight, 0.0f, 1.0f);
        GameObject.WorldPosition = hit.Point + Vector3.UnitY * surfaceOffset;
        var footprint = 0.55f + 0.45f * closeness;
        GameObject.Scale = new Vector3(_baseScale.X * footprint, _baseScale.Y, _baseScale.Z * footprint);
        if (_mesh is not null) _mesh.Emission = _baseEmission * (0.35f + 0.65f * closeness);
    }
}
