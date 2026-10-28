namespace RocketLeg.Scripts;

/// <summary>
/// Gravity in the stadium. The engine's physics world pulls at
/// <see cref="Engine"/>; cars and the ball add the rest themselves, so play
/// feels weighty at this arena's scale without changing the engine.
/// </summary>
public static class Gravity
{
    public const float Engine = 9.81f;
    public const float Stadium = 15.0f;

    /// <summary>Extra downward acceleration a body applies on top of the engine's.</summary>
    public const float Extra = Stadium - Engine;
}
