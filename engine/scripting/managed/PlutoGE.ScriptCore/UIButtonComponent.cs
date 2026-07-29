using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class UIButtonComponent : ComponentReference
{
    private static readonly List<WeakReference<UIButtonComponent>> RegisteredButtons = [];
    private static ulong _lastDispatchSequence;

    internal UIButtonComponent(uint entityId) : base(entityId)
    {
        lock (RegisteredButtons)
            RegisteredButtons.Add(new WeakReference<UIButtonComponent>(this));
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.UIButton;

    public bool Interactable
    {
        get => ScriptBridge.GetUIButtonInteractable(EntityId);
        set => ScriptBridge.SetUIButtonInteractable(EntityId, value);
    }

    public bool IsHovered => ScriptBridge.GetUIButtonHovered(EntityId);
    public bool WasPressed => ScriptBridge.GetUIButtonPressed(EntityId);
    public bool WasReleased => ScriptBridge.GetUIButtonReleased(EntityId);
    public bool WasClicked => ScriptBridge.GetUIButtonClicked(EntityId);

    public event Action? Clicked;
    public event Action? Pressed;
    public event Action? Released;
    public event Action? PointerEntered;
    public event Action? PointerExited;

    private bool _wasHovered;

    /// <summary>
    /// Dispatches edge-triggered managed events from the retained native UI state.
    /// Call once per frame from the owning view/controller.
    /// </summary>
    public void DispatchEvents()
    {
        var hovered = IsHovered;
        if (hovered && !_wasHovered) PointerEntered?.Invoke();
        if (!hovered && _wasHovered) PointerExited?.Invoke();
        if (WasPressed) Pressed?.Invoke();
        if (WasReleased) Released?.Invoke();
        if (WasClicked) Clicked?.Invoke();
        _wasHovered = hovered;
    }

    internal static void DispatchRegisteredEvents(ulong updateSequence)
    {
        if (updateSequence == 0 || updateSequence == _lastDispatchSequence)
            return;
        _lastDispatchSequence = updateSequence;
        lock (RegisteredButtons)
        {
            for (var index = RegisteredButtons.Count - 1; index >= 0; --index)
            {
                if (!RegisteredButtons[index].TryGetTarget(out var button))
                    RegisteredButtons.RemoveAt(index);
                else
                    button.DispatchEvents();
            }
        }
    }
}
