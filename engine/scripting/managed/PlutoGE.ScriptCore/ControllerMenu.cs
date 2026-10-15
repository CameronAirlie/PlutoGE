using System.Diagnostics;

namespace PlutoGE.ScriptCore;

/// <summary>Controller selection shares the mouse actions without fabricating native clicks.</summary>
public class ControllerMenu
{
    private readonly RmlDocument _document;
    private readonly string _rootId;
    private readonly MenuAxisRepeat _horizontal = new(), _vertical = new();
    private long _lastTick = Stopwatch.GetTimestamp();
    private bool _active, _controllerMode;
    private readonly MenuInputGuard _guard = new();
    private string? _highlight;
    private bool? _controllerConnected;
    private bool? _padConnected;
    public MenuNavigation Navigation { get; } = new();
    public Action? Back { get; set; }
    public Action<int>? Tab { get; set; }
    public ControllerMenu(RmlDocument document, string rootId = "menu-body")
    { _document = document; _rootId = rootId; }
    public void UsePointer() { _controllerMode = false; Paint(); }
    public void Add(MenuEntry entry, bool bindClick = true)
    {
        Navigation.Entries.Add(entry);
        if (bindClick) _document.OnClick(entry.Id, () => { if (entry.Enabled) entry.Select(); });
        _document.Element(entry.Id).On("focus", () => FollowPointer(entry.Id, false));
        _document.Element(entry.Id).On("mouseover", () => FollowPointer(entry.Id, true));
    }
    private void FollowPointer(string id, bool pointer)
    {
        if (!_active) return;
        // Scrolling/layout can emit hover events under a stationary mouse.
        // Only actual pointer movement may take selection away from the controller.
        if (pointer && _controllerMode && Input.MouseDelta.LengthSquared() < .01f) return;
        Navigation.Select(id);
        _controllerMode = Input.UsingController;
        Paint();
    }
    public void Open(string? selected = null)
    {
        _active = true; _guard.Reset();
        _controllerMode = Input.UsingController;
        _horizontal.Reset(); _vertical.Reset();
        _lastTick = Stopwatch.GetTimestamp();
        if (selected is not null) Navigation.Select(selected);
        Navigation.EnsureSelection();
        Paint();
    }
    public void Close()
    {
        _active = false; _guard.Reset();
        if (_highlight is not null) ClearHighlight(_highlight);
        _highlight = null;
    }
    public void Update() => Update(MenuFrame.Read(Input.ActiveGamepad >= 0 ? Input.ActiveGamepad : 0));
    public void Update(MenuFrame frame)
    {
        RefreshHints();
        if (!_active) return;
        var now = Stopwatch.GetTimestamp();
        var elapsed = (float)Stopwatch.GetElapsedTime(_lastTick, now).TotalSeconds;
        _lastTick = now;
        Navigation.EnsureSelection();
        if (!_guard.Allow(frame))
        {
            Paint();
            return;
        }
        if (frame.HasInput) _controllerMode = true;
        if (frame.Back) { Back?.Invoke(); Paint(); return; }
        if (frame.PreviousTab || frame.NextTab)
        { Tab?.Invoke(frame.NextTab ? 1 : -1); Navigation.EnsureSelection(); Paint(); return; }
        // Dominant axis avoids diagonal double moves. Vertical wins a tie.
        var vertical = MathF.Abs(frame.Vertical) >= MathF.Abs(frame.Horizontal);
        var y = _vertical.Tick(vertical ? frame.Vertical : 0, elapsed);
        var x = _horizontal.Tick(vertical ? 0 : frame.Horizontal, elapsed);
        if (x != 0 || y != 0) Navigation.Move(x, y);
        if (frame.Secondary) Navigation.Secondary();
        else if (frame.Accept) Navigation.Activate();
        Paint();
    }
    public void RefreshHints()
    {
        var connected = Input.UsingController;
        var padConnected = Input.IsGamepadConnected(Input.ActiveGamepad >= 0 ? Input.ActiveGamepad : 0);
        if (padConnected != _padConnected)
        {
            _padConnected = padConnected;
            _guard.Reset(); _horizontal.Reset(); _vertical.Reset();
        }
        _controllerMode = connected;
        var root = _document.Element(_rootId);
        var marker = connected ? "true" : "false";
        if ((connected != _controllerConnected || root["data-controller-hints"] != marker) && root.SetClass("controller-connected", connected))
        { root["data-controller-hints"] = marker; _controllerConnected = connected; }
    }
    public void Paint()
    {
        var id = _active && _controllerMode ? Navigation.Selected?.Id : null;
        // A native document may be created after OnCreate or replaced on hot reload.
        // The marker belongs to that DOM instance, so stale cached state cannot hide selection.
        if (id == _highlight && (id is null || _document.Element(id)["data-controller-selected"] == "true")) return;
        if (_highlight is not null) ClearHighlight(_highlight);
        _highlight = null;
        if (id is null) return;
        var element = _document.Element(id);
        if (!element.SetClass("controller-focus")) return;
        element["data-controller-selected"] = "true";
        _highlight = id;
        element.ScrollIntoView();
    }
    private void ClearHighlight(string id)
    {
        var element = _document.Element(id);
        element.SetClass("controller-focus", false);
        element["data-controller-selected"] = "false";
    }
}
