using Avalonia.Controls;
using Avalonia.Threading;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Windows;

internal sealed partial class ViewportWindow : Window
{
    private readonly ViewportPaneViewModel _viewModel;
    private DispatcherTimer? _validationResizeTimer;
    private int _validationResizeStep;

    internal ViewportWindow(EditorSessionViewModel session)
    {
        InitializeComponent();
        _viewModel = new ViewportPaneViewModel(session);
        DataContext = _viewModel;
        Closed += OnClosed;
    }

    internal void EnableViewportValidation()
    {
        _viewModel.EnableValidation();
        var sizes = ViewportValidationMath.ResizeSequence;
        _validationResizeTimer = new DispatcherTimer(
            TimeSpan.FromSeconds(2), DispatcherPriority.Background, (_, _) =>
            {
                var size = sizes[_validationResizeStep++ % sizes.Count];
                Width = size.Width;
                Height = size.Height;
            });
        _validationResizeTimer.Start();
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        _validationResizeTimer?.Stop();
        _viewModel.Dispose();
        Closed -= OnClosed;
    }
}
