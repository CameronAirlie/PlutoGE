using System.Collections.ObjectModel;
using System.Windows.Input;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal enum DockLocation
{
    Left,
    Center,
    Right,
    Bottom,
}

internal sealed class DockRegionViewModel : ObservableObject
{
    private DockPaneViewModel? _selectedPane;

    internal DockRegionViewModel(DockLocation location, DockWorkspaceViewModel workspace)
    {
        Location = location;
        Workspace = workspace;
        Panes.CollectionChanged += (_, _) => OnPropertyChanged(nameof(HasPanes));
    }

    public DockLocation Location { get; }
    internal DockWorkspaceViewModel Workspace { get; }
    public string Title => Location.ToString().ToUpperInvariant();
    public ObservableCollection<DockPaneViewModel> Panes { get; } = [];
    public bool HasPanes => Panes.Count > 0;

    public DockPaneViewModel? SelectedPane
    {
        get => _selectedPane;
        set => SetProperty(ref _selectedPane, value);
    }
}

internal sealed class DockPaneViewModel : ObservableObject
{
    private bool _isVisible = true;
    private bool _isFloating;

    internal DockPaneViewModel(string id, string title, object content, DockLocation defaultLocation, DockWorkspaceViewModel workspace)
    {
        Id = id;
        Title = title;
        Content = content;
        DefaultLocation = defaultLocation;
        LastDockLocation = defaultLocation;
        CloseCommand = new RelayCommand(() => workspace.ClosePane(this));
        FloatCommand = new RelayCommand(() => workspace.FloatPane(this));
        DockCommand = new RelayCommand(() => workspace.DockPane(this));
        MovePreviousCommand = new RelayCommand(() => workspace.MovePane(this, -1));
        MoveNextCommand = new RelayCommand(() => workspace.MovePane(this, 1));
        DockLeftCommand = new RelayCommand(() => workspace.MovePane(this, DockLocation.Left));
        DockCenterCommand = new RelayCommand(() => workspace.MovePane(this, DockLocation.Center));
        DockRightCommand = new RelayCommand(() => workspace.MovePane(this, DockLocation.Right));
        DockBottomCommand = new RelayCommand(() => workspace.MovePane(this, DockLocation.Bottom));
    }

    public string Id { get; }
    public string Title { get; }
    public object Content { get; }
    public DockLocation DefaultLocation { get; }
    public DockLocation LastDockLocation { get; internal set; }
    public ICommand CloseCommand { get; }
    public ICommand FloatCommand { get; }
    public ICommand DockCommand { get; }
    public ICommand MovePreviousCommand { get; }
    public ICommand MoveNextCommand { get; }
    public ICommand DockLeftCommand { get; }
    public ICommand DockCenterCommand { get; }
    public ICommand DockRightCommand { get; }
    public ICommand DockBottomCommand { get; }

    public bool IsVisible
    {
        get => _isVisible;
        internal set => SetProperty(ref _isVisible, value);
    }

    public bool IsFloating
    {
        get => _isFloating;
        internal set => SetProperty(ref _isFloating, value);
    }
}

internal sealed class DockWorkspaceViewModel : ObservableObject, IDisposable
{
    private readonly Dictionary<string, DockPaneViewModel> _panes;

    internal DockWorkspaceViewModel(EditorSessionViewModel session)
    {
        Session = session;
        Left = new DockRegionViewModel(DockLocation.Left, this);
        Center = new DockRegionViewModel(DockLocation.Center, this);
        Right = new DockRegionViewModel(DockLocation.Right, this);
        Bottom = new DockRegionViewModel(DockLocation.Bottom, this);

        var viewport = new ViewportPaneViewModel(session);
        _panes = new Dictionary<string, DockPaneViewModel>(StringComparer.OrdinalIgnoreCase)
        {
            ["project"] = new("project", "Project", new ProjectPaneViewModel(session), DockLocation.Bottom, this),
            ["hierarchy"] = new("hierarchy", "Hierarchy", new HierarchyPaneViewModel(session), DockLocation.Left, this),
            ["viewport"] = new("viewport", "Viewport", viewport, DockLocation.Center, this),
            ["editor-camera"] = new("editor-camera", "Editor Camera", new EditorCameraPaneViewModel(session), DockLocation.Right, this),
            ["inspector"] = new("inspector", "Inspector", new InspectorPaneViewModel(session), DockLocation.Right, this),
            ["performance"] = new("performance", "Performance", new PerformancePaneViewModel(viewport.Viewport), DockLocation.Bottom, this),
        };

        ShowProjectCommand = new RelayCommand(() => ShowPane("project"));
        ShowHierarchyCommand = new RelayCommand(() => ShowPane("hierarchy"));
        ShowViewportCommand = new RelayCommand(() => ShowPane("viewport"));
        ShowEditorCameraCommand = new RelayCommand(() => ShowPane("editor-camera"));
        ShowInspectorCommand = new RelayCommand(() => ShowPane("inspector"));
        ShowPerformanceCommand = new RelayCommand(() => ShowPane("performance"));
        RestoreDefaultLayoutCommand = new RelayCommand(RestoreDefaultLayout);
        RestoreDefaultLayout();
    }

    internal event Action<DockPaneViewModel>? FloatRequested;
    internal event Action<DockPaneViewModel>? DockRequested;

    public EditorSessionViewModel Session { get; }
    public DockRegionViewModel Left { get; }
    public DockRegionViewModel Center { get; }
    public DockRegionViewModel Right { get; }
    public DockRegionViewModel Bottom { get; }
    public ICommand ShowProjectCommand { get; }
    public ICommand ShowHierarchyCommand { get; }
    public ICommand ShowViewportCommand { get; }
    public ICommand ShowEditorCameraCommand { get; }
    public ICommand ShowInspectorCommand { get; }
    public ICommand ShowPerformanceCommand { get; }
    public ICommand RestoreDefaultLayoutCommand { get; }

    internal void ShowPane(string id)
    {
        if (!_panes.TryGetValue(id, out var pane)) return;
        if (string.Equals(id, "editor-camera", StringComparison.OrdinalIgnoreCase))
            Session.SceneCamera.RefreshPostProcessing();
        else if (string.Equals(id, "inspector", StringComparison.OrdinalIgnoreCase))
            Session.Inspector.RefreshFromNative(force: true, refreshComponents: true);
        pane.IsVisible = true;
        if (pane.IsFloating)
        {
            FloatRequested?.Invoke(pane);
            return;
        }
        if (FindRegion(pane) is { } current)
        {
            current.SelectedPane = pane;
            return;
        }
        AddToRegion(pane, pane.LastDockLocation);
    }

    internal void ClosePane(DockPaneViewModel pane)
    {
        RemoveFromRegion(pane);
        pane.IsVisible = false;
        if (pane.IsFloating)
        {
            pane.IsFloating = false;
            DockRequested?.Invoke(pane);
        }
    }

    internal void FloatPane(DockPaneViewModel pane)
    {
        if (pane.IsFloating)
        {
            FloatRequested?.Invoke(pane);
            return;
        }
        if (FindRegion(pane) is { } region) pane.LastDockLocation = region.Location;
        RemoveFromRegion(pane);
        pane.IsVisible = true;
        pane.IsFloating = true;
        FloatRequested?.Invoke(pane);
    }

    internal void DockPane(DockPaneViewModel pane)
    {
        if (pane.IsFloating)
        {
            pane.IsFloating = false;
            DockRequested?.Invoke(pane);
        }
        pane.IsVisible = true;
        AddToRegion(pane, pane.LastDockLocation);
    }

    internal void MovePane(DockPaneViewModel pane, DockLocation location)
    {
        if (pane.IsFloating)
        {
            pane.IsFloating = false;
            DockRequested?.Invoke(pane);
        }
        RemoveFromRegion(pane);
        pane.IsVisible = true;
        AddToRegion(pane, location);
    }

    internal void MovePane(DockPaneViewModel pane, int offset)
    {
        var region = FindRegion(pane);
        if (region is null) return;
        var oldIndex = region.Panes.IndexOf(pane);
        var newIndex = Math.Clamp(oldIndex + offset, 0, region.Panes.Count - 1);
        if (oldIndex == newIndex) return;
        region.Panes.Move(oldIndex, newIndex);
        region.SelectedPane = pane;
    }

    internal void MovePaneBefore(DockPaneViewModel pane, DockPaneViewModel target)
    {
        var targetRegion = FindRegion(target);
        if (targetRegion is null || ReferenceEquals(pane, target)) return;
        if (pane.IsFloating)
        {
            pane.IsFloating = false;
            DockRequested?.Invoke(pane);
        }
        RemoveFromRegion(pane);
        var targetIndex = targetRegion.Panes.IndexOf(target);
        targetRegion.Panes.Insert(Math.Max(targetIndex, 0), pane);
        pane.IsVisible = true;
        pane.LastDockLocation = targetRegion.Location;
        targetRegion.SelectedPane = pane;
    }

    internal void RestoreDefaultLayout()
    {
        foreach (var pane in _panes.Values)
        {
            if (pane.IsFloating)
            {
                pane.IsFloating = false;
                DockRequested?.Invoke(pane);
            }
            RemoveFromRegion(pane);
            pane.IsVisible = true;
            AddToRegion(pane, pane.DefaultLocation);
        }
    }

    private void AddToRegion(DockPaneViewModel pane, DockLocation location)
    {
        var region = GetRegion(location);
        if (!region.Panes.Contains(pane)) region.Panes.Add(pane);
        pane.LastDockLocation = location;
        region.SelectedPane = pane;
    }

    private void RemoveFromRegion(DockPaneViewModel pane)
    {
        var region = FindRegion(pane);
        if (region is null) return;
        region.Panes.Remove(pane);
        if (ReferenceEquals(region.SelectedPane, pane)) region.SelectedPane = region.Panes.LastOrDefault();
    }

    private DockRegionViewModel? FindRegion(DockPaneViewModel pane) =>
        new[] { Left, Center, Right, Bottom }.FirstOrDefault(region => region.Panes.Contains(pane));

    private DockRegionViewModel GetRegion(DockLocation location) => location switch
    {
        DockLocation.Left => Left,
        DockLocation.Center => Center,
        DockLocation.Right => Right,
        DockLocation.Bottom => Bottom,
        _ => Center,
    };

    public void Dispose()
    {
        foreach (var disposable in _panes.Values.Select(pane => pane.Content).OfType<IDisposable>()) disposable.Dispose();
    }
}

internal sealed record ProjectPaneViewModel(EditorSessionViewModel Session);
internal sealed record HierarchyPaneViewModel(EditorSessionViewModel Session);
internal sealed record EditorCameraPaneViewModel(EditorSessionViewModel Session);
internal sealed record InspectorPaneViewModel(EditorSessionViewModel Session);
