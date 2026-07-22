using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Platform.Storage;
using PlutoGE.Editor.Avalonia.Native;
using PlutoGE.Editor.Avalonia.ViewModels;
using PlutoGE.Editor.Avalonia.Windows;

namespace PlutoGE.Editor.Avalonia.Services;

internal sealed class EditorWindowService : IDisposable
{
    private readonly Dictionary<string, FloatingPaneWindow> _floatingWindows = new(StringComparer.OrdinalIgnoreCase);
    private EditorShellWindow? _shell;

    internal EditorWindowService(EngineHost host)
    {
        Session = new EditorSessionViewModel(host, OpenProjectAsync, OpenSceneAsync);
        Workspace = new DockWorkspaceViewModel(Session);
        Workspace.FloatRequested += OnFloatRequested;
        Workspace.DockRequested += OnDockRequested;
    }

    internal EditorSessionViewModel Session { get; }
    internal DockWorkspaceViewModel Workspace { get; }

    internal EditorShellWindow CreateShell()
    {
        _shell = new EditorShellWindow(Workspace, this);
        return _shell;
    }

    internal ViewportWindow CreateViewportWindow() => new(Session);

    internal void Exit()
    {
        if (global::Avalonia.Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            desktop.Shutdown();
    }

    private void OnFloatRequested(DockPaneViewModel pane)
    {
        if (_floatingWindows.TryGetValue(pane.Id, out var existing))
        {
            if (existing.WindowState == WindowState.Minimized) existing.WindowState = WindowState.Normal;
            existing.Activate();
            return;
        }

        var window = new FloatingPaneWindow(Workspace, pane);
        _floatingWindows[pane.Id] = window;
        window.Closed += (_, _) => _floatingWindows.Remove(pane.Id);
        window.Show();
    }

    private void OnDockRequested(DockPaneViewModel pane)
    {
        if (!_floatingWindows.Remove(pane.Id, out var window)) return;
        if (window.IsVisible) window.CloseForDock();
    }

    private async Task OpenProjectAsync()
    {
        if (_shell is null) return;
        var files = await _shell.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Open PlutoGE Project",
            AllowMultiple = false,
            FileTypeFilter = [new FilePickerFileType("PlutoGE Project") { Patterns = ["*.plutoproject"] }],
        });
        if (files.Count > 0) Session.LoadProject(files[0].Path.LocalPath);
    }

    private async Task OpenSceneAsync()
    {
        if (_shell is null) return;
        var files = await _shell.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Open PlutoGE Scene",
            AllowMultiple = false,
            FileTypeFilter = [new FilePickerFileType("PlutoGE Scene") { Patterns = ["*.plutoscene"] }],
        });
        if (files.Count > 0) Session.LoadScene(files[0].Path.LocalPath);
    }

    public void Dispose()
    {
        Workspace.FloatRequested -= OnFloatRequested;
        Workspace.DockRequested -= OnDockRequested;
        foreach (var window in _floatingWindows.Values.ToArray())
            if (window.IsVisible) window.CloseForDock();
        _floatingWindows.Clear();
        Workspace.Dispose();
        Session.Dispose();
    }
}
