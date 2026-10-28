using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>
/// Top-right feed of recent match events (goals, demolitions). A fixed set of
/// rows is built once; new entries push older ones down and each fades out.
/// </summary>
public sealed class EventFeedView
{
    private const int Rows = 4;
    private const float RowHeight = 40.0f;
    private const float Lifetime = 5.0f;
    private const float FadeSeconds = 0.6f;

    private readonly HudNode[] _rows = new HudNode[Rows];
    private readonly HudNode[] _labels = new HudNode[Rows];
    private readonly (string Text, Team Team, float Age)[] _entries = new (string, Team, float)[Rows];
    private int _count;

    public EventFeedView(HudNode parent)
    {
        var column = parent.Add("Event Feed").Pin(Anchors.TopRight, new Vector2(32, 32), new Vector2(380, Rows * (RowHeight + 8)));
        for (var row = 0; row < Rows; row++)
        {
            _rows[row] = column.Add($"Row {row}").Pin(Anchors.TopRight, new Vector2(0, row * (RowHeight + 8)), new Vector2(380, RowHeight))
                .Shape("Feed-Item");
            _labels[row] = _rows[row].Add("Text").Fill(14, 0, 14, 0).Label(string.Empty, "Feed-Text", UITextAlignment.MiddleRight);
            _rows[row].Visible = false;
        }
    }

    public void Push(string text, Team team)
    {
        Array.Copy(_entries, 0, _entries, 1, Rows - 1);
        _entries[0] = (text, team, 0.0f);
        _count = Math.Min(_count + 1, Rows);
    }

    public void Clear() => _count = 0;

    public void Update(float deltaTime)
    {
        while (_count > 0 && _entries[_count - 1].Age >= Lifetime) _count--;
        for (var row = 0; row < Rows; row++)
        {
            var visible = row < _count;
            _rows[row].Visible = visible;
            if (!visible) continue;
            ref var entry = ref _entries[row];
            entry.Age += deltaTime;
            _labels[row].Text = entry.Text;
            _rows[row].Styles = entry.Team == Team.Blue ? "Feed-Item-Blue" : "Feed-Item-Orange";
            _rows[row].Opacity = 1.0f - Math.Clamp((entry.Age - (Lifetime - FadeSeconds)) / FadeSeconds, 0.0f, 1.0f);
        }
    }
}
