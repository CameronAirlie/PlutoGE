using System;
using System.Numerics;

namespace RocketLeg.Scripts;

/// <summary>Per-axis air control: angular acceleration from input and damping, in local axes (x pitch, y yaw, z roll).</summary>
public readonly record struct AirControlTuning(Vector3 Acceleration, Vector3 Damping);

/// <summary>
/// The arcade handling model, as pure functions of input and car state so it
/// can be reasoned about (and tested) without physics. The controller applies
/// the results to the rigidbody.
/// <list type="bullet">
/// <item>Ground turning follows a curvature that tightens at low speed and
/// widens at top speed. Yaw is driven to the rate that curvature implies, so
/// the car carves instead of spinning out.</item>
/// <item>Air control accelerates each axis directly. Roll is always damped;
/// pitch and yaw are damped only when their input is released, so a held input
/// keeps rotating and letting go stops the car where it points.</item>
/// <item>Dodges push along the car's heading flattened to the ground plane.
/// Side dodges gain strength with forward speed.</item>
/// </list>
/// </summary>
public static class CarHandling
{
    /// <summary>
    /// Turning curvature in 1/m for a speed fraction (0 = stopped, 1 = top boost
    /// speed). It eases from the low-speed value with the square of the remaining
    /// speed, so it stays tight through mid speeds and widens near the top.
    /// </summary>
    public static float Curvature(float speedFraction, float lowSpeedCurvature, float highSpeedCurvature)
    {
        var remaining = 1.0f - Math.Clamp(speedFraction, 0.0f, 1.0f);
        return highSpeedCurvature + (lowSpeedCurvature - highSpeedCurvature) * remaining * remaining;
    }

    /// <summary>
    /// Yaw rate about the surface normal in rad/s. Steering right (positive)
    /// turns clockwise seen from above the surface, a negative rotation; in
    /// reverse the turn flips, as with real steered wheels.
    /// </summary>
    public static float TargetYawRate(float steer, float forwardSpeed, float curvature) => -steer * curvature * forwardSpeed;

    /// <summary>
    /// Acceleration along the car's heading from throttle. Full power fades to
    /// nothing over the last <paramref name="taper"/> fraction of top speed.
    /// Throttle against the motion brakes; no throttle coasts down.
    /// </summary>
    public static float DriveAcceleration(float throttle, float forwardSpeed, float maximumSpeed, float acceleration,
                                          float brakeAcceleration, float coastDeceleration, float taper = 0.12f)
    {
        if (MathF.Abs(throttle) < 0.01f) return -forwardSpeed * coastDeceleration;
        if (throttle * forwardSpeed < 0.0f && MathF.Abs(forwardSpeed) > 0.25f) return throttle * brakeAcceleration;
        var headroom = (maximumSpeed - MathF.Abs(forwardSpeed)) / MathF.Max(maximumSpeed * taper, 0.01f);
        return throttle * acceleration * Math.Clamp(headroom, 0.0f, 1.0f);
    }

    /// <summary>One air-control step: returns the new local angular velocity.</summary>
    public static Vector3 AirControl(Vector3 localAngular, Vector3 input, in AirControlTuning tuning, float deltaTime)
    {
        input = Vector3.Clamp(input, -Vector3.One, Vector3.One);
        var held = new Vector3(MathF.Abs(input.X), MathF.Abs(input.Y), 0.0f);
        var damping = tuning.Damping * (Vector3.One - held);
        var next = localAngular + input * tuning.Acceleration * deltaTime;
        // Implicit damping is stable for any time step.
        return next / (Vector3.One + damping * deltaTime);
    }

    /// <summary>
    /// Dodge velocity change in flat car axes: X along the car's right, Y along
    /// its heading. <paramref name="direction"/> is the normalised stick (Y forward).
    /// </summary>
    public static Vector2 DodgeImpulse(Vector2 direction, float speedFraction, float forwardImpulse, float sideImpulse,
                                       float sideSpeedBonus)
    {
        var side = sideImpulse * (1.0f + sideSpeedBonus * Math.Clamp(speedFraction, 0.0f, 1.0f));
        return new Vector2(direction.X * side, direction.Y * forwardImpulse);
    }

    /// <summary>
    /// Local spin axis for a dodge, as (right, forward) weights: forward dodges
    /// pitch nose-down, side dodges roll towards their side.
    /// </summary>
    public static Vector2 DodgeSpin(Vector2 direction) => new(-direction.Y, direction.X);
}
