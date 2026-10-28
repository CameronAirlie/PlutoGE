namespace RocketLeg.Scripts;

/// <summary>The two sides of a match. Blue defends the -X goal and attacks +X.</summary>
public enum Team
{
    Blue,
    Orange,
}

public static class TeamExtensions
{
    public static Team Opponent(this Team team) => team == Team.Blue ? Team.Orange : Team.Blue;

    /// <summary>+1 when the team attacks +X, -1 when it attacks -X.</summary>
    public static float AttackSign(this Team team) => team == Team.Blue ? 1.0f : -1.0f;

    public static string DisplayName(this Team team) => team == Team.Blue ? "BLUE" : "ORANGE";

    /// <summary>Scene tag carried by the team's cars and goal pieces.</summary>
    public static string Tag(this Team team) => team == Team.Blue ? "blue" : "orange";
}
