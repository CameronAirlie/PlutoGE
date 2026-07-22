using System.Numerics;

namespace PlutoGE.Editor.Avalonia;

internal static class ViewportCameraController
{
    internal static Vector3 Forward(float yawDegrees, float pitchDegrees)
    {
        var yaw = yawDegrees * MathF.PI / 180.0f;
        var pitch = pitchDegrees * MathF.PI / 180.0f;
        return Vector3.Normalize(new Vector3(
            -MathF.Sin(yaw) * MathF.Cos(pitch),
            MathF.Sin(pitch),
            -MathF.Cos(yaw) * MathF.Cos(pitch)));
    }

    internal static (float Yaw, float Pitch) AdvanceLook(
        float yaw,
        float pitch,
        float deltaX,
        float deltaY,
        float sensitivity = 0.18f)
    {
        return (
            yaw - deltaX * sensitivity,
            Math.Clamp(pitch - deltaY * sensitivity, -89.0f, 89.0f));
    }

    internal static Vector3 AdvancePosition(
        Vector3 position,
        float yawDegrees,
        float pitchDegrees,
        Vector3 localMovement,
        float speed,
        float deltaSeconds)
    {
        if (localMovement.LengthSquared() <= 0.0f)
        {
            return position;
        }

        var forward = Forward(yawDegrees, pitchDegrees);
        var right = Vector3.Normalize(Vector3.Cross(forward, Vector3.UnitY));
        var worldMovement = right * localMovement.X + Vector3.UnitY * localMovement.Y + forward * localMovement.Z;
        return worldMovement.LengthSquared() > 0.0f
            ? position + Vector3.Normalize(worldMovement) * speed * deltaSeconds
            : position;
    }

    internal static Vector3 OrbitPosition(Vector3 pivot, float yawDegrees, float pitchDegrees, float distance) =>
        pivot - Forward(yawDegrees, pitchDegrees) * Math.Max(distance, 0.05f);

    internal static (Vector3 Position, Vector3 Pivot) Pan(
        Vector3 position,
        Vector3 pivot,
        float yawDegrees,
        float pitchDegrees,
        float deltaX,
        float deltaY,
        float unitsPerPixel)
    {
        var forward = Forward(yawDegrees, pitchDegrees);
        var right = Vector3.Normalize(Vector3.Cross(forward, Vector3.UnitY));
        var up = Vector3.Normalize(Vector3.Cross(right, forward));
        var movement = (-right * deltaX + up * deltaY) * unitsPerPixel;
        return (position + movement, pivot + movement);
    }

    internal static float DollyDistance(float distance, float wheelDelta) =>
        Math.Clamp(distance * MathF.Exp(-wheelDelta * 0.15f), 0.05f, 5000.0f);

    internal static float FrameDistance(float maximumScale) =>
        Math.Clamp(Math.Abs(maximumScale) * 3.0f + 1.5f, 2.0f, 5000.0f);
}
