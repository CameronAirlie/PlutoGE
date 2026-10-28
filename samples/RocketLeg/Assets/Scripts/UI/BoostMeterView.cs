using System;
using System.Globalization;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>
/// One player's radial boost gauge in a bottom corner, with a status line for
/// the car's flip, supersonic, powerslide and demolition states.
/// </summary>
public sealed class BoostMeterView
{
    private const float Size = 216.0f;

    private readonly string _fillStyle;
    private readonly HudNode _fill;
    private readonly HudNode _number;
    private readonly HudNode _status;
    private readonly HudNode _root;

    public BoostMeterView(HudNode parent, Team team, int playerNumber, Vector2 anchor)
    {
        _fillStyle = team == Team.Blue ? "Boost-Fill-Blue" : "Boost-Fill-Orange";
        _root = parent.Add($"{team} Boost").Pin(anchor, new Vector2(48, 44), new Vector2(Size, Size + 34));
        var dial = _root.Add("Dial").Pin(Anchors.Top, Vector2.Zero, new Vector2(Size, Size));
        // A fully rounded rect is a filled disc (a procedural circle is only an outline).
        dial.Add("Backdrop").Fill(10, 10, 10, 10).Image(UIImageType.ProceduralRoundedRect, "Boost-Backdrop");
        dial.Add("Track").Fill().Image(UIImageType.ProceduralArc, "Boost-Track").Amount = 1.0f;
        _fill = dial.Add("Fill").Fill().Image(UIImageType.ProceduralArc, _fillStyle);
        _fill.Amount = 1.0f;
        _number = dial.Add("Amount").Centre(Anchors.Middle, new Vector2(0, 10), new Vector2(150, 64)).Label("100", "Boost-Number");
        dial.Add("Caption").Centre(Anchors.Middle, new Vector2(0, -34), new Vector2(150, 18))
            .Label($"P{playerNumber}  BOOST", "Boost-Caption");
        _status = _root.Add("Status").Pin(Anchors.Bottom, Vector2.Zero, new Vector2(Size + 40, 24)).Label(string.Empty, "Car-Status");
    }

    public void Update(ArcadeCarController? car)
    {
        _root.Visible = car is not null;
        if (car is null) return;

        var fraction = car.IsDemolished ? 0.0f : car.BoostFraction;
        _fill.Amount = fraction;
        _fill.Styles = fraction <= 0.0f ? "Boost-Fill-Empty" : _fillStyle;
        _number.Text = MathF.Ceiling(fraction * 100.0f).ToString("0", CultureInfo.InvariantCulture);
        _number.Styles = car.IsBoosting ? "Boost-Number-Active" : "Boost-Number";

        var (status, alert) = Status(car);
        _status.Text = status;
        _status.Styles = alert ? "Car-Status-Alert" : "Car-Status";
    }

    private static (string Text, bool Alert) Status(ArcadeCarController car)
    {
        if (car.IsDemolished)
            return ("DEMOLISHED  " + car.RespawnTimeRemaining.ToString("0.0", CultureInfo.InvariantCulture), true);
        // Frozen cars (kickoff, match over) report as airborne; say nothing.
        if (car.IsFrozen) return (string.Empty, false);
        if (car.IsSupersonic) return ("SUPERSONIC", true);
        if (car.IsPowersliding) return ("POWERSLIDE", false);
        if (car.IsFlipping) return ("FLIP", false);
        if (car.Grounded) return (string.Empty, false);
        if (car.FlipAvailable)
            return ("FLIP " + car.FlipTimeRemaining.ToString("0.0", CultureInfo.InvariantCulture) + "s", false);
        return ("NO FLIP", false);
    }
}
