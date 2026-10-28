using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>
/// Prepares a scene entity's native screen canvas: native backend, the
/// RocketLeg theme, and scaling with the window against a 1920x1080 reference
/// (matching height, so the layout keeps its proportions on any aspect ratio).
/// <see cref="Root"/> is a full-viewport element to build under; a rebuild
/// replaces the previous tree.
/// </summary>
public sealed class HudScreen
{
    public const string ThemePath = "project://UI/Themes/RocketLeg.plutotheme";
    public static readonly Vector2 ReferenceResolution = new(1920, 1080);

    private HudScreen(HudNode root) => Root = root;

    public HudNode Root { get; }

    public static HudScreen Create(GameObject entity, int sortingOrder = 0, string rootName = "Root")
    {
        var canvas = entity.GetComponent<CanvasComponent>() ?? entity.AddComponent<CanvasComponent>()
            ?? throw new InvalidOperationException($"{entity.Name} cannot take a Canvas");
        if (canvas.Backend != UIRenderBackend.Native)
        {
            Debug.LogWarning($"{entity.Name}: canvas switched to the native UI backend (set Backend to Native in the scene).");
            canvas.Backend = UIRenderBackend.Native;
        }
        canvas.RenderMode = CanvasRenderMode.ScreenSpace;
        canvas.ScaleMode = CanvasScaleMode.ScaleWithScreenSize;
        canvas.ReferenceResolution = ReferenceResolution;
        canvas.MatchWidthOrHeight = 1.0f;
        canvas.SortingOrder = sortingOrder;

        var theme = entity.GetComponent<UIThemeComponent>() ?? entity.AddComponent<UIThemeComponent>();
        if (theme is not null && string.IsNullOrEmpty(theme.Theme)) theme.Theme = ThemePath;

        // One root per screen: a scene that reloads keeps nothing from the last build.
        foreach (var child in entity.Children)
            if (child.Name == rootName)
                child.Destroy();
        var root = HudNode.Wrap(GameObject.Create(rootName, entity) ?? throw new InvalidOperationException("No scene to build UI in"));
        root.Fill();
        return new HudScreen(root);
    }
}
