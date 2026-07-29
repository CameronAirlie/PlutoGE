using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum UIAnchorPreset
{
    TopLeft = 0,
    TopCenter = 1,
    TopRight = 2,
    MiddleLeft = 3,
    MiddleCenter = 4,
    MiddleRight = 5,
    BottomLeft = 6,
    BottomCenter = 7,
    BottomRight = 8,
    Stretch = 9,
}

public enum UILayoutMode { None, Horizontal, Vertical, Grid }

public sealed class RectTransformComponent : ComponentReference
{
    internal RectTransformComponent(uint entityId) : base(entityId) {}

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.RectTransform;

    public Vector2 AnchoredPosition
    {
        get => ScriptBridge.GetRectAnchoredPosition(EntityId);
        set => ScriptBridge.SetRectAnchoredPosition(EntityId, value);
    }

    public Vector2 SizeDelta
    {
        get => ScriptBridge.GetRectSizeDelta(EntityId);
        set => ScriptBridge.SetRectSizeDelta(EntityId, value);
    }

    public UIAnchorPreset AnchorPreset
    {
        get => (UIAnchorPreset)ScriptBridge.GetRectAnchorPreset(EntityId);
        set => ScriptBridge.SetRectAnchorPreset(EntityId, (int)value);
    }

    public float Rotation
    {
        get => ScriptBridge.GetRectRotation(EntityId);
        set => ScriptBridge.SetRectRotation(EntityId, value);
    }

    public float Opacity
    {
        get => ScriptBridge.GetRectOpacity(EntityId);
        set => ScriptBridge.SetRectOpacity(EntityId, value);
    }

    public Vector2 LocalScale
    {
        get => ScriptBridge.GetRectLocalScale(EntityId);
        set => ScriptBridge.SetRectLocalScale(EntityId, value);
    }

    public UILayoutMode LayoutMode
    {
        get => (UILayoutMode)ScriptBridge.GetRectLayoutMode(EntityId);
        set => ScriptBridge.SetRectLayoutMode(EntityId, (int)value);
    }
}
