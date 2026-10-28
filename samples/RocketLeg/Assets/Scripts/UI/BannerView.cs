using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>
/// Large centre-screen messages: the kickoff count, GO, goals and overtime.
/// Each message pops in and fades out after its hold time.
/// </summary>
public sealed class BannerView
{
    private const float PopSeconds = 0.25f;
    private const float FadeSeconds = 0.35f;

    private readonly HudNode _root;
    private readonly HudNode _headline;
    private readonly HudNode _detail;
    private float _age;
    private float _hold;
    private int _shownCount = -1;

    public BannerView(HudNode parent)
    {
        _root = parent.Add("Banner").Centre(Anchors.Middle, new Vector2(0, 120), new Vector2(1200, 240));
        _headline = _root.Add("Headline").Centre(Anchors.Middle, new Vector2(0, 26), new Vector2(1200, 170)).Label(string.Empty, "Banner");
        _detail = _root.Add("Detail").Centre(Anchors.Bottom, new Vector2(0, 22), new Vector2(1200, 40)).Label(string.Empty, "Banner-Detail");
        _root.Visible = false;
    }

    /// <summary>Shows the whole seconds left on a kickoff count (3, 2, 1), once per number.</summary>
    public void ShowCountdown(float secondsLeft, bool overtime)
    {
        var count = Math.Max((int)MathF.Ceiling(secondsLeft), 1);
        if (count == _shownCount) return;
        _shownCount = count;
        Show(count.ToString(), overtime ? "OVERTIME  ·  NEXT GOAL WINS" : string.Empty, "Countdown", 1.0f);
    }

    public void ShowGo() => Show("GO!", string.Empty, "Countdown", 0.6f);

    public void ShowGoal(in GoalEvent goal)
    {
        var who = HudText.Credit(goal);
        var detail = goal.OwnGoal ? $"OWN GOAL  ·  {who}" : $"{who}  ·  {MathF.Round(goal.BallSpeed * 3.6f)} KM/H";
        Show(goal.OwnGoal ? "OWN GOAL!" : "GOAL!", detail, goal.Team == Team.Blue ? "Banner-Blue" : "Banner-Orange", 2.4f);
    }

    public void Hide()
    {
        _root.Visible = false;
        _shownCount = -1;
    }

    public void Update(float deltaTime)
    {
        if (!_root.Visible) return;
        _age += deltaTime;
        var pop = Math.Clamp(_age / PopSeconds, 0.0f, 1.0f);
        // Overshoot slightly, then settle at full size.
        _root.Scale = 1.0f + 0.35f * (1.0f - pop) * (1.0f - pop) - 0.06f * MathF.Sin(pop * MathF.PI);
        var fade = Math.Clamp((_age - _hold) / FadeSeconds, 0.0f, 1.0f);
        _root.Opacity = 1.0f - fade;
        if (fade >= 1.0f) _root.Visible = false;
    }

    private void Show(string headline, string detail, string style, float hold)
    {
        _headline.Text = headline;
        _headline.Styles = style;
        _detail.Text = detail;
        _detail.Visible = detail.Length > 0;
        _age = 0.0f;
        _hold = hold;
        _root.Opacity = 1.0f;
        _root.Visible = true;
        if (style != "Countdown") _shownCount = -1;
    }
}
