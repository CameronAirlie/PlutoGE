namespace RocketLeg.Scripts;

public enum MatchPhase
{
    /// <summary>Cars and ball are frozen at kickoff positions while the count runs.</summary>
    Countdown,
    /// <summary>Live play. The clock waits for the first touch after each kickoff.</summary>
    Playing,
    /// <summary>A goal went in: the ball is gone, cars may keep driving, the clock is stopped.</summary>
    GoalScored,
    MatchOver,
}

/// <summary>A scored goal. <see cref="Scorer"/> is null when nobody touched the ball since kickoff.</summary>
public readonly record struct GoalEvent(Team Team, ArcadeCarController? Scorer, float BallSpeed, bool OwnGoal, bool Overtime);

public readonly record struct DemolitionEvent(ArcadeCarController Attacker, ArcadeCarController Victim);

/// <summary>Per-car tallies shown on the end-of-match card.</summary>
public sealed class PlayerStats
{
    public int Goals { get; set; }
    public int Demolitions { get; set; }
    public int Touches { get; set; }

    public void Reset() => Goals = Demolitions = Touches = 0;
}

/// <summary>Read-only match state a presenter polls each frame.</summary>
/// <param name="ClockSeconds">Regulation time remaining, or time played in overtime.</param>
/// <param name="CountdownSeconds">Seconds left on the kickoff count while in <see cref="MatchPhase.Countdown"/>.</param>
public readonly record struct MatchSnapshot(
    MatchPhase Phase,
    int BlueScore,
    int OrangeScore,
    float ClockSeconds,
    bool Overtime,
    bool ClockRunning,
    float CountdownSeconds,
    Team? Winner)
{
    public int Score(Team team) => team == Team.Blue ? BlueScore : OrangeScore;
}
