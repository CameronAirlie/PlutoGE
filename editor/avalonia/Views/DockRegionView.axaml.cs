using Avalonia.Controls;
using Avalonia.Input;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Views;

internal sealed partial class DockRegionView : UserControl
{
    private static readonly DataFormat<DockPaneViewModel> PaneDataFormat =
        DataFormat.CreateInProcessFormat<DockPaneViewModel>("PlutoGE.DockPane");

    public DockRegionView() => InitializeComponent();

    private async void OnDragHandlePressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control { DataContext: DockPaneViewModel pane }) return;
        var data = new DataTransfer();
        data.Add(DataTransferItem.Create(PaneDataFormat, pane));
        await DragDrop.DoDragDropAsync(e, data, DragDropEffects.Move);
    }

    private void OnDragOver(object? sender, DragEventArgs e)
    {
        e.DragEffects = e.DataTransfer.Contains(PaneDataFormat) ? DragDropEffects.Move : DragDropEffects.None;
    }

    private void OnRegionDrop(object? sender, DragEventArgs e)
    {
        if (DataContext is not DockRegionViewModel region || e.DataTransfer.TryGetValue(PaneDataFormat) is not { } pane) return;
        region.Workspace.MovePane(pane, region.Location);
        e.Handled = true;
    }

    private void OnTabDrop(object? sender, DragEventArgs e)
    {
        if (sender is not Control { DataContext: DockPaneViewModel target } ||
            e.DataTransfer.TryGetValue(PaneDataFormat) is not { } pane) return;
        if (DataContext is DockRegionViewModel region) region.Workspace.MovePaneBefore(pane, target);
        e.Handled = true;
    }
}
