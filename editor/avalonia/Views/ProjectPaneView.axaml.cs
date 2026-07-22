using Avalonia.Controls;
using Avalonia.Input;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Views;

internal sealed partial class ProjectPaneView : UserControl
{
    public ProjectPaneView() => InitializeComponent();

    private async void OnAssetDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (sender is ListBox { SelectedItem: AssetItemViewModel item } && DataContext is ProjectPaneViewModel viewModel)
            await viewModel.Session.ActivateBrowserItemAsync(item);
    }
}
