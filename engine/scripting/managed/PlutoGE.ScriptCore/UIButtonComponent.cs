using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class UIButtonComponent : ComponentReference
{
    internal UIButtonComponent(uint entityId) : base(entityId) {}

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
}
