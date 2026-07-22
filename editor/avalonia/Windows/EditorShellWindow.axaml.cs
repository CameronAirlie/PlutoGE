using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Input;
using PlutoGE.Editor.Avalonia.Services;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Windows;

internal sealed partial class EditorShellWindow : Window
{
    private readonly EditorWindowService _windows;

    internal EditorShellWindow(DockWorkspaceViewModel workspace, EditorWindowService windows)
    {
        _windows = windows;
        InitializeComponent();
        DataContext = workspace;
    }

    private void OnExit(object? sender, RoutedEventArgs e) => _windows.Exit();

    private void OnKeyDown(object? sender, KeyEventArgs e)
    {
        if (DataContext is not DockWorkspaceViewModel workspace ||
            FocusManager?.GetFocusedElement() is TextBox or NumericUpDown)
            return;

        var command = e.Key == Key.Delete
            ? workspace.Session.DeleteEntityCommand
            : e.Key == Key.D && e.KeyModifiers.HasFlag(KeyModifiers.Control)
                ? workspace.Session.DuplicateEntityCommand
                : null;
        if (command?.CanExecute(null) != true) return;
        command.Execute(null);
        e.Handled = true;
    }
}
