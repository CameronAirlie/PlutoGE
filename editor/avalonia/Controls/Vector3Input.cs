using Avalonia;
using Avalonia.Controls.Primitives;
using Avalonia.Data;

namespace PlutoGE.Editor.Avalonia.Controls;

internal sealed class Vector3Input : TemplatedControl
{
    public static readonly StyledProperty<string> LabelProperty =
        AvaloniaProperty.Register<Vector3Input, string>(nameof(Label), string.Empty);

    public static readonly StyledProperty<decimal> XProperty =
        AvaloniaProperty.Register<Vector3Input, decimal>(nameof(X), 0m, defaultBindingMode: BindingMode.TwoWay);

    public static readonly StyledProperty<decimal> YProperty =
        AvaloniaProperty.Register<Vector3Input, decimal>(nameof(Y), 0m, defaultBindingMode: BindingMode.TwoWay);

    public static readonly StyledProperty<decimal> ZProperty =
        AvaloniaProperty.Register<Vector3Input, decimal>(nameof(Z), 0m, defaultBindingMode: BindingMode.TwoWay);

    public static readonly StyledProperty<decimal> IncrementProperty =
        AvaloniaProperty.Register<Vector3Input, decimal>(nameof(Increment), 0.1m);

    public static readonly StyledProperty<decimal> MinimumProperty =
        AvaloniaProperty.Register<Vector3Input, decimal>(nameof(Minimum), decimal.MinValue);

    public static readonly StyledProperty<decimal> MaximumProperty =
        AvaloniaProperty.Register<Vector3Input, decimal>(nameof(Maximum), decimal.MaxValue);

    public string Label { get => GetValue(LabelProperty); set => SetValue(LabelProperty, value); }
    public decimal X { get => GetValue(XProperty); set => SetValue(XProperty, value); }
    public decimal Y { get => GetValue(YProperty); set => SetValue(YProperty, value); }
    public decimal Z { get => GetValue(ZProperty); set => SetValue(ZProperty, value); }
    public decimal Increment { get => GetValue(IncrementProperty); set => SetValue(IncrementProperty, value); }
    public decimal Minimum { get => GetValue(MinimumProperty); set => SetValue(MinimumProperty, value); }
    public decimal Maximum { get => GetValue(MaximumProperty); set => SetValue(MaximumProperty, value); }
}
