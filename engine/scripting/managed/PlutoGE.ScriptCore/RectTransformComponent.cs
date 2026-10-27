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

    // Through the generic property bridge. UI coordinates are y-up: (0,0) is the parent's bottom-left corner.

    private const string Type = "RectTransformComponent";

    /// <summary>Lower-left anchor as a fraction of the parent (0,0 bottom-left; 1,1 top-right).</summary>
    public Vector2 AnchorMin { get => ComponentProperties.GetVector2(EntityId, Type, "AnchorMin"); set => ComponentProperties.SetVector2(EntityId, Type, "AnchorMin", value); }
    public Vector2 AnchorMax { get => ComponentProperties.GetVector2(EntityId, Type, "AnchorMax"); set => ComponentProperties.SetVector2(EntityId, Type, "AnchorMax", value); }
    public Vector2 Pivot { get => ComponentProperties.GetVector2(EntityId, Type, "Pivot"); set => ComponentProperties.SetVector2(EntityId, Type, "Pivot", value); }

    /// <summary>Insets from the resolved rect: left, bottom, right, top.</summary>
    public Vector4 Margin { get => ComponentProperties.GetVector4(EntityId, Type, "Margin"); set => ComponentProperties.SetVector4(EntityId, Type, "Margin", value); }
    public Vector2 MinimumSize { get => ComponentProperties.GetVector2(EntityId, Type, "MinimumSize"); set => ComponentProperties.SetVector2(EntityId, Type, "MinimumSize", value); }
    public Vector2 MaximumSize { get => ComponentProperties.GetVector2(EntityId, Type, "MaximumSize"); set => ComponentProperties.SetVector2(EntityId, Type, "MaximumSize", value); }
    public bool ClipChildren { get => ComponentProperties.GetBool(EntityId, Type, "ClipChildren"); set => ComponentProperties.SetBool(EntityId, Type, "ClipChildren", value); }

    /// <summary>Whether the element can take the pointer; static HUD graphics usually turn it off.</summary>
    public bool RaycastTarget { get => ComponentProperties.GetBool(EntityId, Type, "RaycastTarget"); set => ComponentProperties.SetBool(EntityId, Type, "RaycastTarget", value); }

    /// <summary>Anchors, pivot, position and size in one call, e.g. to stretch over the parent with insets.</summary>
    public void Place(Vector2 anchorMin, Vector2 anchorMax, Vector2 pivot, Vector2 anchoredPosition, Vector2 sizeDelta)
    {
        AnchorMin = anchorMin;
        AnchorMax = anchorMax;
        Pivot = pivot;
        AnchoredPosition = anchoredPosition;
        SizeDelta = sizeDelta;
    }
}
