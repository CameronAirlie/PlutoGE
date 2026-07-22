using Avalonia.Controls;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Windows;

internal sealed partial class ProjectSettingsWindow : Window
{
    private readonly ProjectSettingsViewModel _viewModel;

    internal ProjectSettingsWindow(EditorSessionViewModel session)
    {
        InitializeComponent();
        _viewModel = new ProjectSettingsViewModel(session);
        _viewModel.CloseRequested += OnCloseRequested;
        DataContext = _viewModel;
        Closed += (_, _) => _viewModel.CloseRequested -= OnCloseRequested;
    }

    private void OnCloseRequested(bool saved) => Close(saved);
}
