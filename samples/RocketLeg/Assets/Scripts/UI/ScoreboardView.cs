using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>Top-centre scoreboard: team scores either side of the match clock.</summary>
public sealed class ScoreboardView
{
    private const float PopSeconds = 0.45f;

    private readonly HudNode _blueScore;
    private readonly HudNode _orangeScore;
    private readonly HudNode _clock;
    private readonly HudNode _caption;
    private float _bluePop;
    private float _orangePop;

    public ScoreboardView(HudNode parent)
    {
        var panel = parent.Add("Scoreboard").Pin(Anchors.Top, Vector2.Zero, new Vector2(470, 104)).Shape("Score-Panel");
        var blue = panel.Add("Blue").Pin(Anchors.Left, Vector2.Zero, new Vector2(138, 104)).Shape("Score-Blue");
        _blueScore = blue.Add("Score").Centre(Anchors.Middle, new Vector2(0, 8), new Vector2(138, 70)).Label("0", "Score-Number");
        blue.Add("Name").Centre(Anchors.Bottom, new Vector2(0, 16), new Vector2(138, 20)).Label(Team.Blue.DisplayName(), "Team-Name");
        var orange = panel.Add("Orange").Pin(Anchors.Right, Vector2.Zero, new Vector2(138, 104)).Shape("Score-Orange");
        _orangeScore = orange.Add("Score").Centre(Anchors.Middle, new Vector2(0, 8), new Vector2(138, 70)).Label("0", "Score-Number");
        orange.Add("Name").Centre(Anchors.Bottom, new Vector2(0, 16), new Vector2(138, 20)).Label(Team.Orange.DisplayName(), "Team-Name");
        _clock = panel.Add("Clock").Centre(Anchors.Middle, new Vector2(0, 8), new Vector2(194, 56)).Label("5:00", "Clock");
        _caption = panel.Add("Caption").Centre(Anchors.Bottom, new Vector2(0, 17), new Vector2(194, 18)).Label(string.Empty, "Clock-Caption");
    }

    public void Pop(Team team)
    {
        if (team == Team.Blue) _bluePop = PopSeconds;
        else _orangePop = PopSeconds;
    }

    public void Update(in MatchSnapshot match, float deltaTime)
    {
        _blueScore.Text = match.BlueScore.ToString();
        _orangeScore.Text = match.OrangeScore.ToString();
        _clock.Text = (match.Overtime ? "+" : string.Empty) + FormatClock(match.ClockSeconds, match.Overtime);
        _clock.Styles = match.Overtime ? "Clock-Overtime" : "Clock";
        _caption.Styles = match.Overtime ? "Clock-Caption-Overtime" : "Clock-Caption";
        _caption.Text = match.Phase == MatchPhase.MatchOver ? "FINAL" : match.Overtime ? "OVERTIME" :
            match.Phase == MatchPhase.Playing && !match.ClockRunning ? "KICKOFF" : string.Empty;

        _bluePop = MathF.Max(0.0f, _bluePop - deltaTime);
        _orangePop = MathF.Max(0.0f, _orangePop - deltaTime);
        _blueScore.Scale = PopScale(_bluePop);
        _orangeScore.Scale = PopScale(_orangePop);
    }

    // Regulation counts down and rounds up (0:00.4 still shows 0:01); overtime counts up.
    public static string FormatClock(float seconds, bool countingUp)
    {
        var whole = countingUp ? (int)MathF.Floor(seconds) : (int)MathF.Ceiling(MathF.Max(seconds, 0.0f));
        return $"{whole / 60}:{whole % 60:00}";
    }

    private static float PopScale(float remaining)
    {
        if (remaining <= 0.0f) return 1.0f;
        var t = 1.0f - remaining / PopSeconds;
        return 1.0f + 0.45f * MathF.Sin(t * MathF.PI) * (1.0f - t);
    }
}
