using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Smooth chase camera for a vehicle. Attach this to a Camera entity and assign
/// the target car in the Inspector.
/// </summary>
public sealed class VehicleFollowCamera : ScriptBehaviour
{
    [SerializedField] private GameObject? target = null;
    [SerializedField] private Vector3 localOffset = new(0.0f, 2.2f, 6.8f);
    [SerializedField] private Vector3 lookOffset = new(0.0f, 1.0f, 0.0f);
    [SerializedField] private float positionSharpness = 7.0f;
    [SerializedField] private float verticalPositionSharpness = 4.0f;
    [SerializedField] private float rotationSharpness = 10.0f;
    [SerializedField] private float headingSharpness = 5.0f;
    [SerializedField] private float focusSharpness = 12.0f;
    [SerializedField] private float velocitySharpness = 18.0f;
    [SerializedField] private float targetLeadTime = 0.035f;
    [SerializedField] private float maxLeadDistance = 3.0f;
    [SerializedField] private float collisionPadding = 0.18f;

    private Vector3 _smoothedWorldPosition;
    private Vector3 _smoothedFocusPoint;
    private Vector3 _smoothedForward = -Vector3.UnitZ;
    private Vector3 _smoothedTargetVelocity;
    private Vector3 _smoothedRotation;
    private RigidbodyComponent? _targetRigidbody;
    private bool _initialized;

    public override void OnCreate()
    {
        if (target is not null)
        {
            _targetRigidbody = target.GetComponent<RigidbodyComponent>();
            _smoothedForward = GetTargetFlatForward();
            var targetPosition = target.WorldPosition;
            _smoothedWorldPosition = ComputeDesiredWorldPosition(targetPosition, localOffset, _smoothedForward);
            _smoothedFocusPoint = targetPosition + lookOffset;
            GameObject.Position = WorldOffsetToTargetLocal(_smoothedWorldPosition - targetPosition);
            _smoothedRotation = ComputeLocalLookRotation(_smoothedWorldPosition, _smoothedFocusPoint);
            GameObject.Rotation = _smoothedRotation;
            _initialized = true;
        }
    }

    public override void OnLateUpdate(float deltaTime)
    {
        if (target is null || deltaTime <= 0.0f)
        {
            return;
        }

        _targetRigidbody ??= target.GetComponent<RigidbodyComponent>();
        var targetPosition = target.WorldPosition;
        var velocityAmount = 1.0f - MathF.Exp(-velocitySharpness * deltaTime);
        var targetVelocity = _targetRigidbody?.Velocity ?? Vector3.Zero;
        _smoothedTargetVelocity = Vector3.Lerp(_smoothedTargetVelocity, targetVelocity, velocityAmount);
        var leadOffset = ClampMagnitude(_smoothedTargetVelocity * MathF.Max(targetLeadTime, 0.0f), MathF.Max(maxLeadDistance, 0.0f));
        var predictedTargetPosition = targetPosition + leadOffset;

        var focusPoint = predictedTargetPosition + lookOffset;
        var headingAmount = 1.0f - MathF.Exp(-headingSharpness * deltaTime);
        _smoothedForward = SafeNormalize(Vector3.Lerp(_smoothedForward, GetTargetFlatForward(), headingAmount), GetTargetFlatForward());
        var desiredWorldPosition = ComputeDesiredWorldPosition(predictedTargetPosition, localOffset, _smoothedForward);

        var cameraRay = desiredWorldPosition - focusPoint;
        var rayLength = cameraRay.Length();
        if (rayLength > 0.0001f &&
            Physics.Raycast(focusPoint, cameraRay / rayLength, rayLength, target, out var hit))
        {
            desiredWorldPosition = focusPoint + cameraRay / rayLength * MathF.Max(0.05f, hit.Distance - collisionPadding);
        }

        if (!_initialized)
        {
            _smoothedWorldPosition = desiredWorldPosition;
            _smoothedFocusPoint = focusPoint;
            _smoothedRotation = ComputeLocalLookRotation(desiredWorldPosition, focusPoint);
            _initialized = true;
        }

        var positionAmount = 1.0f - MathF.Exp(-positionSharpness * deltaTime);
        var verticalAmount = 1.0f - MathF.Exp(-verticalPositionSharpness * deltaTime);
        var focusAmount = 1.0f - MathF.Exp(-focusSharpness * deltaTime);
        _smoothedWorldPosition = new Vector3(
            Lerp(_smoothedWorldPosition.X, desiredWorldPosition.X, positionAmount),
            Lerp(_smoothedWorldPosition.Y, desiredWorldPosition.Y, verticalAmount),
            Lerp(_smoothedWorldPosition.Z, desiredWorldPosition.Z, positionAmount));
        _smoothedFocusPoint = Vector3.Lerp(_smoothedFocusPoint, focusPoint, focusAmount);
        GameObject.Position = WorldOffsetToTargetLocal(_smoothedWorldPosition - targetPosition);

        var desiredRotation = ComputeLocalLookRotation(_smoothedWorldPosition, _smoothedFocusPoint);
        var rotationAmount = 1.0f - MathF.Exp(-rotationSharpness * deltaTime);
        _smoothedRotation = new Vector3(
            NormalizeAngle(LerpAngle(_smoothedRotation.X, desiredRotation.X, rotationAmount)),
            NormalizeAngle(LerpAngle(_smoothedRotation.Y, desiredRotation.Y, rotationAmount)),
            NormalizeAngle(LerpAngle(_smoothedRotation.Z, desiredRotation.Z, rotationAmount)));
        GameObject.Rotation = _smoothedRotation;
    }

    private Vector3 ComputeDesiredWorldPosition(Vector3 targetPosition, Vector3 localPosition, Vector3 forward)
    {
        if (target is null)
        {
            return GameObject.WorldPosition;
        }

        var chaseForward = SafeNormalize(forward, -Vector3.UnitZ);
        var right = SafeNormalize(Vector3.Cross(chaseForward, Vector3.UnitY), Vector3.UnitX);
        return targetPosition +
               right * localPosition.X +
               Vector3.UnitY * localPosition.Y -
               chaseForward * localPosition.Z;
    }

    private Vector3 WorldOffsetToTargetLocal(Vector3 worldOffset)
    {
        if (target is null)
        {
            return worldOffset;
        }

        var right = SafeNormalize(target.Right, Vector3.UnitX);
        var forward = SafeNormalize(target.Forward, -Vector3.UnitZ);
        return new Vector3(
            Vector3.Dot(worldOffset, right),
            worldOffset.Y,
            -Vector3.Dot(worldOffset, forward));
    }

    private Vector3 ComputeLocalLookRotation(Vector3 position, Vector3 focusPoint)
    {
        var direction = focusPoint - position;
        if (direction.LengthSquared() <= 0.000001f)
        {
            return Vector3.Zero;
        }

        direction = Vector3.Normalize(direction);
        var targetForward = SafeNormalize(_smoothedForward, -Vector3.UnitZ);
        var targetRight = SafeNormalize(Vector3.Cross(targetForward, Vector3.UnitY), Vector3.UnitX);
        var localRight = Vector3.Dot(direction, targetRight);
        var localForward = Vector3.Dot(direction, targetForward);
        var localYaw = MathF.Atan2(-localRight, localForward) * 180.0f / MathF.PI;
        var horizontal = MathF.Sqrt(direction.X * direction.X + direction.Z * direction.Z);
        var pitch = MathF.Atan2(direction.Y, horizontal) * 180.0f / MathF.PI;
        return new Vector3(pitch, NormalizeAngle(localYaw), 0.0f);
    }

    private Vector3 GetTargetFlatForward()
    {
        var forward = target?.Forward ?? -Vector3.UnitZ;
        forward.Y = 0.0f;
        return SafeNormalize(forward, _smoothedForward.LengthSquared() > 0.000001f ? _smoothedForward : -Vector3.UnitZ);
    }

    private static Vector3 SafeNormalize(Vector3 value, Vector3 fallback)
    {
        return value.LengthSquared() > 0.000001f ? Vector3.Normalize(value) : fallback;
    }

    private static Vector3 ClampMagnitude(Vector3 value, float maxLength)
    {
        if (maxLength <= 0.0f)
        {
            return Vector3.Zero;
        }

        var lengthSquared = value.LengthSquared();
        if (lengthSquared <= maxLength * maxLength)
        {
            return value;
        }

        return Vector3.Normalize(value) * maxLength;
    }

    private static float LerpAngle(float from, float to, float amount)
    {
        return from + DeltaAngle(from, to) * Math.Clamp(amount, 0.0f, 1.0f);
    }

    private static float Lerp(float from, float to, float amount)
    {
        return from + (to - from) * Math.Clamp(amount, 0.0f, 1.0f);
    }

    private static float DeltaAngle(float from, float to)
    {
        var delta = (to - from) % 360.0f;
        if (delta > 180.0f) delta -= 360.0f;
        if (delta < -180.0f) delta += 360.0f;
        return delta;
    }

    private static float NormalizeAngle(float angle)
    {
        angle %= 360.0f;
        if (angle > 180.0f) angle -= 360.0f;
        if (angle < -180.0f) angle += 360.0f;
        return angle;
    }
}
