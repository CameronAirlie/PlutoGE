using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>Controls reference, toggled with the ToggleHelp action; a hint stays visible while it is closed.</summary>
public sealed class HelpPanelView
{
    private const string Controls =
        "<b><color=#4FA0FF>P1</color></b>  W/S drive · A/D steer · Q/E air roll · Left Shift boost · Space jump · Left Ctrl powerslide<br>" +
        "<b><color=#FF8A2A>P2</color></b>  Arrows drive and steer · Comma/Period air roll · Right Shift boost · Enter jump · Right Ctrl powerslide<br>" +
        "<b>PAD</b>  RT/LT drive · Left stick steer and aim · A jump · B boost · LB powerslide / free air roll · X air roll<br><br>" +
        "Jump again in the air: neutral for a double jump, with a direction to dodge. Hold jump for a higher jump.<br>" +
        "Boost into an opponent at supersonic speed to demolish them. Big pads fill the tank, small pads add 12.<br><br>" +
        "<b>C</b> ball cam · <b>Tab</b> switch followed car · <b>R</b> restart match · <b>H</b> hide this panel";

    private readonly HudNode _panel;
    private readonly HudNode _hint;

    public HelpPanelView(HudNode parent)
    {
        _panel = parent.Add("Help").Centre(Anchors.Bottom, new Vector2(0, 230), new Vector2(1080, 290)).Shape("Card");
        _panel.Add("Title").Pin(Anchors.TopLeft, new Vector2(28, 20), new Vector2(600, 30))
            .Label("CONTROLS", "Card-Title", UITextAlignment.MiddleLeft);
        _panel.Add("Body").Fill(28, 20, 28, 62).Label(Controls, "Card-Body", UITextAlignment.TopLeft).Wrapping();
        _hint = parent.Add("Help Hint").Pin(Anchors.Bottom, new Vector2(0, 18), new Vector2(400, 20))
            .Label("H  CONTROLS", "Hint");
        IsOpen = false;
    }

    public bool IsOpen
    {
        get => _panel.Visible;
        set
        {
            _panel.Visible = value;
            _hint.Visible = !value;
        }
    }
}
