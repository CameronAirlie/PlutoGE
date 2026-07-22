using Avalonia.Controls;
using Avalonia.Input.Platform;
using Avalonia.Interactivity;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Views;

internal sealed partial class PerformancePaneView : UserControl
{
    public PerformancePaneView() => InitializeComponent();

    private async void OnCopyMetrics(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not PerformancePaneViewModel viewModel) return;
        var clipboard = TopLevel.GetTopLevel(this)?.Clipboard;
        if (clipboard is null)
        {
            viewModel.SetCopyStatus("Clipboard unavailable");
            return;
        }

        try
        {
            await clipboard.SetTextAsync(viewModel.BuildMetricsReport());
            viewModel.SetCopyStatus("Copied");
        }
        catch (Exception exception)
        {
            viewModel.SetCopyStatus($"Copy failed: {exception.Message}");
        }
    }
}
