using Avalonia.Controls;
using Avalonia.Interactivity;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Views;

internal sealed partial class InspectorPaneView : UserControl
{
    public InspectorPaneView() => InitializeComponent();

    private void OnAddComponentClick(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button button || DataContext is not InspectorPaneViewModel viewModel) return;

        var menu = new MenuFlyout();
        foreach (var category in viewModel.Session.Inspector.AddableComponentTypes.GroupBy(type => type.Category))
        {
            var categoryMenu = new MenuItem { Header = category.Key };
            foreach (var componentType in category)
            {
                categoryMenu.Items.Add(new MenuItem
                {
                    Header = componentType.DisplayName,
                    Command = componentType.AddCommand,
                });
            }
            menu.Items.Add(categoryMenu);
        }

        if (menu.Items.Count == 0)
            menu.Items.Add(new MenuItem { Header = "No more component types available", IsEnabled = false });
        menu.ShowAt(button);
    }
}
