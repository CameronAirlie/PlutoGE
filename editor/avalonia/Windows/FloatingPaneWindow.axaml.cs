using Avalonia.Controls;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Windows;

internal sealed partial class FloatingPaneWindow : Window
{
    private readonly DockWorkspaceViewModel _workspace;
    private readonly DockPaneViewModel _pane;
    private bool _suppressRedock;

    internal FloatingPaneWindow(DockWorkspaceViewModel workspace, DockPaneViewModel pane)
    {
        _workspace = workspace;
        _pane = pane;
        InitializeComponent();
        DataContext = pane;
        Title = $"PlutoGE — {pane.Title}";
        Closed += OnClosed;
    }

    internal void CloseForDock()
    {
        _suppressRedock = true;
        Close();
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        Closed -= OnClosed;
        if (!_suppressRedock && _pane.IsFloating) _workspace.DockPane(_pane);
    }
}
