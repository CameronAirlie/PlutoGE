using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>End-of-match card: the winner, the final score and each player's stats.</summary>
public sealed class MatchResultView
{
    // Left edge and width of each column: player, goals, demolitions, touches.
    private static readonly (float X, float Width)[] Columns = { (18, 190), (230, 90), (330, 90), (430, 92) };
    private static readonly string[] Headings = { "PLAYER", "GOALS", "DEMOS", "TOUCHES" };

    private readonly HudNode _card;
    private readonly HudNode _title;
    private readonly HudNode _score;
    private readonly HudNode[,] _cells = new HudNode[2, Columns.Length];

    public MatchResultView(HudNode parent)
    {
        _card = parent.Add("Match Result").Centre(Anchors.Middle, new Vector2(0, 20), new Vector2(620, 390)).Shape("Card");
        _title = _card.Add("Title").Pin(Anchors.Top, new Vector2(0, 24), new Vector2(560, 96)).Label(string.Empty, "Banner");
        _score = _card.Add("Score").Pin(Anchors.Top, new Vector2(0, 120), new Vector2(560, 36)).Label(string.Empty, "Card-Title");
        var table = _card.Add("Table").Pin(Anchors.Top, new Vector2(0, 170), new Vector2(540, 150));
        for (var column = 0; column < Columns.Length; column++)
            Cell(table, column, 0, 22).Label(Headings[column], "Caption", Alignment(column));
        for (var row = 0; row < 2; row++)
        {
            var top = 30 + row * 52;
            table.Add($"Row {row}").Pin(Anchors.TopLeft, new Vector2(0, top), new Vector2(540, 44)).Shape("Card-Row");
            for (var column = 0; column < Columns.Length; column++)
                _cells[row, column] = Cell(table, column, top, 44).Label(string.Empty, "Stat-Value", Alignment(column));
        }
        _card.Add("Hint").Pin(Anchors.Bottom, new Vector2(0, 22), new Vector2(560, 20)).Label("PRESS R FOR A REMATCH", "Hint");
        _card.Visible = false;
    }

    public void Show(in MatchSnapshot match, RocketLegMatch rules)
    {
        _title.Text = match.Winner is { } winner ? $"{winner.DisplayName()} WINS" : "DRAW";
        _title.Styles = match.Winner switch { Team.Blue => "Banner-Blue", Team.Orange => "Banner-Orange", _ => "Banner" };
        _score.Text = $"<color=#4FA0FF>{match.BlueScore}</color>  -  <color=#FF8A2A>{match.OrangeScore}</color>";
        for (var row = 0; row < 2; row++)
        {
            var team = row == 0 ? Team.Blue : Team.Orange;
            var car = rules.Car(team);
            var stats = car is null ? new PlayerStats() : rules.Stats(car);
            var colour = team == Team.Blue ? "#4FA0FF" : "#FF8A2A";
            _cells[row, 0].Text = $"<color={colour}>{team.DisplayName()} P{car?.PlayerNumber ?? row + 1}</color>";
            _cells[row, 1].Text = stats.Goals.ToString();
            _cells[row, 2].Text = stats.Demolitions.ToString();
            _cells[row, 3].Text = stats.Touches.ToString();
        }
        _card.Visible = true;
    }

    public void Hide() => _card.Visible = false;

    private static HudNode Cell(HudNode table, int column, float top, float height) =>
        table.Add($"Cell {column}").Pin(Anchors.TopLeft, new Vector2(Columns[column].X, top), new Vector2(Columns[column].Width, height));

    private static UITextAlignment Alignment(int column) => column == 0 ? UITextAlignment.MiddleLeft : UITextAlignment.MiddleCenter;
}
