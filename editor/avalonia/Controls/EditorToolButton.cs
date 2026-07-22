using Avalonia;
using Avalonia.Controls;

namespace PlutoGE.Editor.Avalonia.Controls;

internal sealed class EditorToolButton : Button
{
    public static readonly StyledProperty<string> GlyphProperty =
        AvaloniaProperty.Register<EditorToolButton, string>(nameof(Glyph), string.Empty);

    public static readonly StyledProperty<string> LabelProperty =
        AvaloniaProperty.Register<EditorToolButton, string>(nameof(Label), string.Empty);

    public static readonly StyledProperty<string> ShortcutProperty =
        AvaloniaProperty.Register<EditorToolButton, string>(nameof(Shortcut), string.Empty);

    public string Glyph
    {
        get => GetValue(GlyphProperty);
        set => SetValue(GlyphProperty, value);
    }

    public string Label
    {
        get => GetValue(LabelProperty);
        set => SetValue(LabelProperty, value);
    }

    public string Shortcut
    {
        get => GetValue(ShortcutProperty);
        set => SetValue(ShortcutProperty, value);
    }
}
