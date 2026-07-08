using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Stable chase camera for a vehicle. For the least jitter, parent this camera
/// under the target car, attach this script to the Camera entity, and assign the
/// target car in the Inspector.
/// </summary>
public sealed class VehicleFollowCamera : ScriptBehaviour
{
    [SerializedField] private GameObject? target = null;
    [SerializedField] private Vector3 localOffset = new(0.0f, 2.2f, 6.8f);
    [SerializedField] private Vector3 lookOffset = new(0.0f, 1.0f, 0.0f);
    [SerializedField] private bool useParentedLocalPosition = true;
    [SerializedField] private float positionSharpness = 0.0f;
    [SerializedField] private float verticalPositionSharpness = 0.0f;
    [SerializedField] private float rotationSharpness = 0.0f;
    [SerializedField] private float headingSharpness = 0.0f;
    [SerializedField] private float focusSharpness = 0.0f;
    [SerializedField] private float velocitySharpness = 18.0f;
    [SerializedField] private float targetLeadTime = 0.0f;
    [SerializedField] private float maxLeadDistance = 0.0f;
    [SerializedField] private bool avoidObstacles = false;
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
            if (useParentedLocalPosition)
            {
                GameObject.Position = localOffset;
                _smoothedWorldPosition = GameObject.WorldPosition;
            }
            else
            {
                _smoothedWorldPosition = ComputeDesiredWorldPosition(targetPosition, localOffset, _smoothedForward);
                GameObject.WorldPosition = _smoothedWorldPosition;
            }

            _smoothedFocusPoint = targetPosition + lookOffset;
            _smoothedRotation = ComputeWorldLookRotation(_smoothedWorldPosition, _smoothedFocusPoint);
            GameObject.WorldRotation = _smoothedRotation;
            _initialized = true;
        }
    }

    public override void OnLateUpdate(float deltaTime)
    {
        if (target is null || deltaTime <= 0.0f)
        {
            return;
        }

        var targetPosition = target.WorldPosition;
        var leadOffset = Vector3.Zero;
        if (targetLeadTime > 0.0f && maxLeadDistance > 0.0f)
        {
            _targetRigidbody ??= target.GetComponent<RigidbodyComponent>();
            var velocityAmount = DampAmount(velocitySharpness, deltaTime);
            var targetVelocity = _targetRigidbody?.Velocity ?? Vector3.Zero;
            _smoothedTargetVelocity = Vector3.Lerp(_smoothedTargetVelocity, targetVelocity, velocityAmount);
            leadOffset = ClampMagnitude(_smoothedTargetVelocity * targetLeadTime, maxLeadDistance);
        }
        else
        {
            _smoothedTargetVelocity = Vector3.Zero;
        }

        var predictedTargetPosition = targetPosition + leadOffset;

        var focusPoint = predictedTargetPosition + lookOffset;
        var headingAmount = DampAmount(headingSharpness, deltaTime);
        _smoothedForward = SafeNormalize(Vector3.Lerp(_smoothedForward, GetTargetFlatForward(), headingAmount), GetTargetFlatForward());
        var desiredWorldPosition = ComputeDesiredWorldPosition(predictedTargetPosition, localOffset, _smoothedForward);

        var cameraRay = desiredWorldPosition - focusPoint;
        var rayLength = cameraRay.Length();
        if (!useParentedLocalPosition &&
            avoidObstacles &&
            rayLength > 0.0001f &&
            Physics.Raycast(focusPoint, cameraRay / rayLength, rayLength, target, out var hit))
        {
            desiredWorldPosition = focusPoint + cameraRay / rayLength * MathF.Max(0.05f, hit.Distance - collisionPadding);
        }

        if (!_initialized)
        {
            if (useParentedLocalPosition)
            {
                GameObject.Position = localOffset;
                _smoothedWorldPosition = GameObject.WorldPosition;
            }
            else
            {
                _smoothedWorldPosition = desiredWorldPosition;
            }

            _smoothedFocusPoint = focusPoint;
            _smoothedRotation = ComputeWorldLookRotation(_smoothedWorldPosition, focusPoint);
            _initialized = true;
        }

        if (useParentedLocalPosition)
        {
            GameObject.Position = localOffset;
            _smoothedWorldPosition = GameObject.WorldPosition;
        }
        else
        {
            var positionAmount = DampAmount(positionSharpness, deltaTime);
            var verticalAmount = DampAmount(verticalPositionSharpness, deltaTime);
            _smoothedWorldPosition = new Vector3(
                Lerp(_smoothedWorldPosition.X, desiredWorldPosition.X, positionAmount),
                Lerp(_smoothedWorldPosition.Y, desiredWorldPosition.Y, verticalAmount),
                Lerp(_smoothedWorldPosition.Z, desiredWorldPosition.Z, positionAmount));
            GameObject.WorldPosition = _smoothedWorldPosition;
        }

        var focusAmount = DampAmount(focusSharpness, deltaTime);
        _smoothedFocusPoint = Vector3.Lerp(_smoothedFocusPoint, focusPoint, focusAmount);

        var desiredRotation = ComputeWorldLookRotation(_smoothedWorldPosition, _smoothedFocusPoint);
        var rotationAmount = DampAmount(rotationSharpness, deltaTime);
        _smoothedRotation = new Vector3(
            NormalizeAngle(LerpAngle(_smoothedRotation.X, desiredRotation.X, rotationAmount)),
            NormalizeAngle(LerpAngle(_smoothedRotation.Y, desiredRotation.Y, rotationAmount)),
            NormalizeAngle(LerpAngle(_smoothedRotation.Z, desiredRotation.Z, rotationAmount)));
        GameObject.WorldRotation = _smoothedRotation;
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

    private static Vector3 ComputeWorldLookRotation(Vector3 position, Vector3 focusPoint)
    {
        var direction = focusPoint - position;
        if (direction.LengthSquared() <= 0.000001f)
        {
            return Vector3.Zero;
        }

        direction = Vector3.Normalize(direction);
        var yaw = MathF.Atan2(-direction.X, -direction.Z);
        var horizontal = MathF.Sqrt(direction.X * direction.X + direction.Z * direction.Z);
        var pitch = MathF.Atan2(direction.Y, horizontal);

        // PlutoGE composes Euler rotations as Rx * Ry * Rz. A no-roll camera
        // should behave as world yaw followed by pitch, so convert that desired
        // orientation into the engine's XYZ Euler representation.
        var sinYaw = MathF.Sin(yaw);
        var cosYaw = MathF.Cos(yaw);
        var sinPitch = MathF.Sin(pitch);
        var cosPitch = MathF.Cos(pitch);
        var radiansToDegrees = 180.0f / MathF.PI;

        return new Vector3(
            MathF.Atan2(sinPitch, cosYaw * cosPitch) * radiansToDegrees,
            MathF.Asin(Math.Clamp(sinYaw * cosPitch, -1.0f, 1.0f)) * radiansToDegrees,
            MathF.Atan2(-sinYaw * sinPitch, cosYaw) * radiansToDegrees);
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

    private static float DampAmount(float sharpness, float deltaTime)
    {
        if (sharpness <= 0.0f)
        {
            return 1.0f;
        }

        return 1.0f - MathF.Exp(-sharpness * deltaTime);
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
