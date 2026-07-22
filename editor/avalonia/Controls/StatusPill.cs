using Avalonia;
using Avalonia.Controls.Primitives;

namespace PlutoGE.Editor.Avalonia.Controls;

internal sealed class StatusPill : TemplatedControl
{
    public static readonly StyledProperty<string> TextProperty =
        AvaloniaProperty.Register<StatusPill, string>(nameof(Text), string.Empty);

    public string Text
    {
        get => GetValue(TextProperty);
        set => SetValue(TextProperty, value);
    }
}
