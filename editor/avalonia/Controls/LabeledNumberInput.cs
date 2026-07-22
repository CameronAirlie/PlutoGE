using Avalonia;
using Avalonia.Controls.Primitives;

namespace PlutoGE.Editor.Avalonia.Controls;

internal sealed class LabeledNumberInput : TemplatedControl
{
    public static readonly StyledProperty<string> LabelProperty =
        AvaloniaProperty.Register<LabeledNumberInput, string>(nameof(Label), string.Empty);

    public static readonly StyledProperty<decimal?> ValueProperty =
        AvaloniaProperty.Register<LabeledNumberInput, decimal?>(nameof(Value));

    public static readonly StyledProperty<decimal> IncrementProperty =
        AvaloniaProperty.Register<LabeledNumberInput, decimal>(nameof(Increment), 0.1m);

    public string Label { get => GetValue(LabelProperty); set => SetValue(LabelProperty, value); }
    public decimal? Value { get => GetValue(ValueProperty); set => SetValue(ValueProperty, value); }
    public decimal Increment { get => GetValue(IncrementProperty); set => SetValue(IncrementProperty, value); }
}
