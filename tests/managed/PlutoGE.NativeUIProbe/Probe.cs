using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.NativeUIProbe;

// Exercises the native UI scripting surface; the C++ test reads the results from serialized fields.
public sealed class UIProbe : ScriptBehaviour
{
    [SerializedField] public bool PropertiesWork;
    [SerializedField] public bool ComponentsWork;
    [SerializedField] public float ObservedValue = -1.0f;
    [SerializedField] public int ObservedEvents;
    [SerializedField] public bool HierarchyWorks;
    [SerializedField] public string Failure = string.Empty;

    private readonly ObservableValue<float> _volume = new(0.0f);
    private IDisposable? _binding;

    public override void OnCreate()
    {
        try
        {
            var sliderObject = GameObject.Find("Slider") ?? throw new InvalidOperationException("No slider entity");
            var slider = sliderObject.GetComponent<UISliderComponent>() ?? throw new InvalidOperationException("No slider component");
            slider.Minimum = 0.0f;
            slider.Maximum = 10.0f;
            slider.Value = 4.0f;
            slider.ValueChanged += value => { ObservedValue = value; ObservedEvents++; };
            _binding = UIBind.Value(slider, _volume);

            var shape = sliderObject.AddComponent<UIShapeComponent>() ?? throw new InvalidOperationException("AddComponent failed");
            shape.CornerRadius = 6.0f;
            shape.Color = new Vector4(0.2f, 0.4f, 0.6f, 1.0f);
            PropertiesWork = MathF.Abs(slider.Value - 0.0f) < 1e-4f // the binding pushed its initial value
                && slider.Maximum == 10.0f
                && shape.CornerRadii == new Vector4(6.0f)
                && MathF.Abs(shape.Color.Y - 0.4f) < 1e-4f
                && slider.Enabled;

            slider.Enabled = false;
            var disabled = !slider.Enabled;
            slider.Enabled = true;
            ComponentsWork = disabled && sliderObject.HasComponent<UISliderComponent>()
                && sliderObject.GetComponent<UIToggleComponent>() is null
                && GameObject.Find("Slider")!.GetComponent<UIShapeComponent>()?.Exists == true;

            HierarchyWorks = BuildFromCode(GameObject.Find("Canvas") ?? throw new InvalidOperationException("No canvas entity"));
        }
        catch (Exception exception)
        {
            Failure = exception.ToString();
        }
    }

    // Creates a panel with two labels under the canvas, reorders and reparents them, and reads the tree back.
    private static bool BuildFromCode(GameObject canvas)
    {
        var panel = GameObject.Create("Built Panel", canvas) ?? throw new InvalidOperationException("Create failed");
        var rect = panel.AddComponent<RectTransformComponent>() ?? throw new InvalidOperationException("No RectTransform");
        rect.AnchorMin = Vector2.Zero;
        rect.AnchorMax = Vector2.One;
        rect.RaycastTarget = false; // it covers the slider the C++ test presses
        var label = panel.AddComponent<UITextComponent>() ?? throw new InvalidOperationException("No UI Text");
        label.Text = "Built";
        label.LetterSpacing = 2.0f;
        var first = GameObject.Create("First", panel)!;
        var second = GameObject.Create("Second", panel)!;
        first.AddComponent<UILayoutItemComponent>();
        second.SetSiblingIndex(0);
        var ordered = panel.Children.Select(child => child.Name).SequenceEqual(["Second", "First"]);
        var nested = GameObject.Create("Nested", second)!;
        var found = panel.FindChild("Second/Nested")?.EntityId == nested.EntityId && panel.FindChild("Missing") is null;
        var moved = nested.SetParent(first) && nested.Parent?.EntityId == first.EntityId && second.Children.Length == 0;
        var refused = !panel.SetParent(nested); // a descendant cannot become the parent
        var rooted = GameObject.Create("Root Entity") is { } root && root.Parent is null && first.Parent?.EntityId == panel.EntityId;
        return ordered && found && moved && refused && rooted && panel.Parent?.EntityId == canvas.EntityId &&
               rect.AnchorMax == Vector2.One && !rect.RaycastTarget && label.LetterSpacing == 2.0f && label.Text == "Built";
    }

    public override void OnUpdate(float deltaTime)
    {
        // The two-way binding mirrors pointer edits into the observable.
        if (ObservedEvents > 0 && MathF.Abs(_volume.Value - ObservedValue) > 1e-4f)
            Failure = $"Binding saw {_volume.Value}, event saw {ObservedValue}";
    }

    public override void OnDestroy() => _binding?.Dispose();
}
