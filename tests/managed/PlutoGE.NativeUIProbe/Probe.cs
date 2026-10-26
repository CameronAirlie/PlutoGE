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
        }
        catch (Exception exception)
        {
            Failure = exception.ToString();
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        // The two-way binding mirrors pointer edits into the observable.
        if (ObservedEvents > 0 && MathF.Abs(_volume.Value - ObservedValue) > 1e-4f)
            Failure = $"Binding saw {_volume.Value}, event saw {ObservedValue}";
    }

    public override void OnDestroy() => _binding?.Dispose();
}
