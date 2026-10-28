namespace RocketLeg.Scripts.UI;

/// <summary>Shared wording for HUD views.</summary>
internal static class HudText
{
    public static string PlayerName(ArcadeCarController car) => $"{car.Team.DisplayName()} P{car.PlayerNumber}";

    /// <summary>Who to credit for a goal: the scorer, or the team when nobody touched the ball.</summary>
    public static string Credit(in GoalEvent goal) => goal.Scorer is null ? goal.Team.DisplayName() : PlayerName(goal.Scorer);
}
