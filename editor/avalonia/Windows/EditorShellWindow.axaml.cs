using Avalonia.Controls;
using Avalonia.Interactivity;
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
}
