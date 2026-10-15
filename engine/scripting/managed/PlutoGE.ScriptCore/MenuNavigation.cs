namespace PlutoGE.ScriptCore;

public static class MenuValues
{
    public static float Step(float value, float step, float min, float max, int direction) =>
        Math.Clamp(MathF.Round((value + step * direction - min) / step) * step + min, min, max);
}

/// <summary>Opening input cannot also activate the first item in a new menu.</summary>
public sealed class MenuInputGuard
{
    private bool _armed;
    public void Reset() => _armed = false;
    public bool Allow(MenuFrame frame)
    {
        if (_armed) return true;
        _armed = frame.Neutral;
        return false;
    }
}

/// <summary>Menu dismissal cannot also fire gameplay actions held on that frame.</summary>
public sealed class MenuCapture
{
    private bool _open, _waitForRelease;
    public bool RequiresNeutral => !_open && _waitForRelease;
    public void Set(bool open) { _open = open; _waitForRelease = true; }
    public bool Allows(bool cursorLocked, bool neutral)
    {
        if (_open || !cursorLocked) return false;
        if (neutral) _waitForRelease = false;
        return !_waitForRelease;
    }
}

public sealed record MenuEntry(string Id, int Row, int Column, Action Select,
    Func<bool>? Available = null, Action<int>? Adjust = null, Action? Secondary = null)
{
    public bool Enabled => Available?.Invoke() ?? true;
}

/// <summary>Logical menu grid independent of native DOM handles.</summary>
public sealed class MenuNavigation
{
    public List<MenuEntry> Entries { get; } = [];
    public MenuEntry? Selected { get; private set; }
    public void Select(string id) => Selected = Entries.FirstOrDefault(e => e.Id == id && e.Enabled) ?? Selected;
    public void EnsureSelection()
    {
        if (Selected?.Enabled != true) Selected = Entries.FirstOrDefault(e => e.Enabled);
    }
    public void Move(int horizontal, int vertical)
    {
        EnsureSelection();
        if (Selected is not { } current) return;
        if (horizontal != 0 && current.Adjust is { } adjust)
        {
            adjust(horizontal);
            return;
        }
        // Prefer the next row in the chosen direction, retaining the nearest column.
        // No wrap: a held stick cannot unexpectedly cycle into a destructive action.
        var candidates = Entries.Where(e => e.Enabled && e != current &&
            (vertical != 0 ? (e.Row - current.Row) * vertical > 0 :
                e.Row == current.Row && (e.Column - current.Column) * horizontal > 0));
        Selected = (vertical != 0
            ? candidates.OrderBy(e => Math.Abs(e.Row - current.Row)).ThenBy(e => Math.Abs(e.Column - current.Column))
            : candidates.OrderBy(e => Math.Abs(e.Column - current.Column))).FirstOrDefault() ?? current;
    }
    public void Activate() { EnsureSelection(); Selected?.Select(); }
    public void Secondary() { EnsureSelection(); Selected?.Secondary?.Invoke(); }
}

/// <summary>Stick hysteresis and timed repeat; time is supplied by an unscaled clock.</summary>
public sealed class MenuAxisRepeat
{
    private int _direction;
    private float _remaining;
    public void Reset() { _direction = 0; _remaining = 0; }
    public int Tick(float value, float elapsed)
    {
        if (!float.IsFinite(value)) value = 0;
        var sign = Math.Sign(value);
        var direction = MathF.Abs(value) >= (sign == _direction ? .30f : .55f) ? sign : 0;
        if (direction == 0) { Reset(); return 0; }
        if (direction != _direction)
        { _direction = direction; _remaining = .35f; return direction; }
        _remaining -= float.IsFinite(elapsed) ? Math.Clamp(elapsed, 0, .1f) : 0;
        if (_remaining > 0) return 0;
        _remaining += .10f;
        return direction;
    }
}

public readonly record struct MenuFrame(float Horizontal, float Vertical, bool Accept, bool Back,
    bool PreviousTab, bool NextTab, bool Secondary, bool Space, bool Done, bool Neutral)
{
    public bool HasInput => MathF.Abs(Horizontal) >= .55f || MathF.Abs(Vertical) >= .55f ||
        Accept || Back || PreviousTab || NextTab || Secondary || Space || Done;
    public static MenuFrame Read(int gamepad = 0)
    {
        float x = Input.GetGamepadAxis(GamepadAxis.LeftX, gamepad);
        float y = Input.GetGamepadAxis(GamepadAxis.LeftY, gamepad);
        bool Down(GamepadButton b) => Input.IsGamepadButtonDown(b, gamepad);
        bool Pressed(GamepadButton b) => Input.IsGamepadButtonPressed(b, gamepad);
        if (Down(GamepadButton.DpadLeft)) x = -1;
        if (Down(GamepadButton.DpadRight)) x = 1;
        if (Down(GamepadButton.DpadUp)) y = -1;
        if (Down(GamepadButton.DpadDown)) y = 1;
        return new(x, y, Pressed(GamepadButton.A), Pressed(GamepadButton.B),
            Pressed(GamepadButton.LeftBumper), Pressed(GamepadButton.RightBumper),
            Pressed(GamepadButton.X), Pressed(GamepadButton.Y), Pressed(GamepadButton.Start),
            MathF.Abs(x) < .30f && MathF.Abs(y) < .30f &&
            !Down(GamepadButton.A) && !Down(GamepadButton.B) && !Down(GamepadButton.X) &&
            !Down(GamepadButton.Y) && !Down(GamepadButton.Start) &&
            !Down(GamepadButton.LeftBumper) && !Down(GamepadButton.RightBumper));
    }
}
