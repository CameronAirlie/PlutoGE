using System.Collections.ObjectModel;
using System.Windows.Input;
using Avalonia.Threading;
using PlutoGE.Editor.Avalonia.Native;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class EditorSessionViewModel : ObservableObject, IDisposable
{
    private readonly EngineHost _host;
    private readonly DispatcherTimer _refreshTimer;
    private IReadOnlyList<EntityNode> _allEntities = [];
    private EntityNodeViewModel? _selectedEntity;
    private AssetItemViewModel? _selectedScene;
    private string _searchText = string.Empty;
    private string _statusText = "Open the Viewport to start PlutoGE";
    private string _engineStatusText = "ENGINE WAITING";
    private string _projectName = "No project loaded";
    private string _projectPath = string.Empty;
    private string _assetDirectory = string.Empty;
    private string _activeScene = "Untitled scene";
    private bool _disposed;

    internal EditorSessionViewModel(EngineHost host, Func<Task> openProject, Func<Task> openScene)
    {
        _host = host;
        SceneCamera = new SceneCameraSettingsViewModel(host);
        Inspector = new TransformInspectorViewModel(host);
        OpenProjectCommand = new AsyncRelayCommand(openProject);
        OpenSceneCommand = new AsyncRelayCommand(openScene);
        OpenSelectedSceneCommand = new RelayCommand(OpenSelectedScene, () => SelectedScene is not null);

        _host.EngineReady += OnEngineReady;
        _host.StatusChanged += OnStatusChanged;
        _refreshTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        _refreshTimer.Tick += OnRefreshTick;
        _refreshTimer.Start();
    }

    internal event Action<uint>? SelectionChanged;
    internal EngineHost Host => _host;

    public ObservableCollection<EntityNodeViewModel> Hierarchy { get; } = [];
    public ObservableCollection<AssetItemViewModel> Assets { get; } = [];
    public ObservableCollection<AssetItemViewModel> Scenes { get; } = [];
    public TransformInspectorViewModel Inspector { get; }
    public SceneCameraSettingsViewModel SceneCamera { get; }
    public ICommand OpenProjectCommand { get; }
    public ICommand OpenSceneCommand { get; }
    public RelayCommand OpenSelectedSceneCommand { get; }

    public EntityNodeViewModel? SelectedEntity
    {
        get => _selectedEntity;
        set
        {
            if (!SetProperty(ref _selectedEntity, value)) return;
            Inspector.Select(value);
            SelectionChanged?.Invoke(value?.Id ?? 0);
        }
    }

    public AssetItemViewModel? SelectedScene
    {
        get => _selectedScene;
        set
        {
            if (!SetProperty(ref _selectedScene, value)) return;
            OpenSelectedSceneCommand.RaiseCanExecuteChanged();
        }
    }

    public string SearchText
    {
        get => _searchText;
        set
        {
            if (SetProperty(ref _searchText, value ?? string.Empty)) RebuildHierarchy();
        }
    }

    public string StatusText { get => _statusText; private set => SetProperty(ref _statusText, value); }
    public string EngineStatusText { get => _engineStatusText; private set => SetProperty(ref _engineStatusText, value); }
    public string ProjectName { get => _projectName; private set => SetProperty(ref _projectName, value); }
    public string ProjectPath { get => _projectPath; private set => SetProperty(ref _projectPath, value); }
    public string AssetDirectory { get => _assetDirectory; private set => SetProperty(ref _assetDirectory, value); }
    public string ActiveScene { get => _activeScene; private set => SetProperty(ref _activeScene, value); }

    internal void LoadProject(string path)
    {
        try
        {
            ApplyProject(_host.LoadProject(path));
            LoadHierarchy();
            SceneCamera.RefreshPostProcessing();
            Inspector.RefreshFromNative(force: true, refreshComponents: true);
            ActiveScene = FormatSceneName(_host.ReadScenePath());
            StatusText = $"Opened project: {ProjectName}";
            EngineStatusText = "ENGINE LIVE";
        }
        catch (Exception exception)
        {
            ReportError(exception.Message);
        }
    }

    internal void LoadScene(string pathOrReference)
    {
        try
        {
            ActiveScene = FormatSceneName(_host.LoadScene(pathOrReference));
            LoadHierarchy();
            StatusText = $"Opened scene: {ActiveScene}";
        }
        catch (Exception exception)
        {
            ReportError(exception.Message);
        }
    }

    internal void SelectEntity(uint entityId)
    {
        if (entityId == 0)
        {
            SelectedEntity = null;
            return;
        }
        var entity = FindEntity(Hierarchy, entityId);
        if (entity is null && !string.IsNullOrEmpty(SearchText))
        {
            SearchText = string.Empty;
            entity = FindEntity(Hierarchy, entityId);
        }
        SelectedEntity = entity;
    }

    private void OpenSelectedScene()
    {
        if (SelectedScene is { } scene) LoadScene(scene.Reference);
    }

    private void OnEngineReady(object? sender, EventArgs e) => Dispatcher.UIThread.Post(() =>
    {
        if (_host.ReadProject() is { } project) ApplyProject(project);
        LoadHierarchy();
        SceneCamera.RefreshPostProcessing();
        Inspector.RefreshFromNative(force: true, refreshComponents: true);
        ActiveScene = FormatSceneName(_host.ReadScenePath());
        StatusText = "PlutoGE connected";
        EngineStatusText = "ENGINE LIVE";
    });

    private void OnStatusChanged(object? sender, string status) => Dispatcher.UIThread.Post(() => ReportError(status));

    private void ApplyProject(ProjectDocument project)
    {
        ProjectName = project.Name;
        ProjectPath = project.ManifestPath;
        AssetDirectory = project.AssetDirectory;
        Assets.Clear();
        Scenes.Clear();
        foreach (var asset in project.Assets)
        {
            var item = new AssetItemViewModel(asset.Reference, asset.Type, asset.IsScene, FormatSize(asset.Size));
            Assets.Add(item);
            if (item.IsScene) Scenes.Add(item);
        }
        SelectedScene = null;
    }

    private void LoadHierarchy()
    {
        _allEntities = _host.ReadHierarchy();
        RebuildHierarchy();
        if (_allEntities.Count == 0) StatusText = "Scene is empty";
    }

    private void RebuildHierarchy()
    {
        var previousSelection = SelectedEntity?.Id;
        Hierarchy.Clear();
        foreach (var node in _allEntities)
        {
            var filtered = BuildNode(node, SearchText);
            if (filtered is not null) Hierarchy.Add(filtered);
        }
        SelectedEntity = previousSelection is { } id ? FindEntity(Hierarchy, id) : null;
    }

    private static EntityNodeViewModel? BuildNode(EntityNode node, string searchText)
    {
        var children = node.Children.Select(child => BuildNode(child, searchText))
            .Where(child => child is not null).Cast<EntityNodeViewModel>().ToArray();
        var matches = string.IsNullOrWhiteSpace(searchText) || node.Name.Contains(searchText, StringComparison.OrdinalIgnoreCase);
        return matches || children.Length > 0 ? new EntityNodeViewModel(node.Id, node.Name, children) : null;
    }

    private static EntityNodeViewModel? FindEntity(IEnumerable<EntityNodeViewModel> nodes, uint id)
    {
        foreach (var node in nodes)
        {
            if (node.Id == id) return node;
            if (FindEntity(node.Children, id) is { } nested) return nested;
        }
        return null;
    }

    private void OnRefreshTick(object? sender, EventArgs e)
    {
        if (!_host.IsReady) return;
        Inspector.RefreshFromNative(refreshComponents: Inspector.HasSelection && Inspector.Components.Count == 0);
        if (!SceneCamera.PostProcessingLoaded)
            SceneCamera.RefreshPostProcessing();
    }

    private void ReportError(string message)
    {
        StatusText = message;
        EngineStatusText = "ENGINE NOTICE";
    }

    private static string FormatSceneName(string path) => string.IsNullOrWhiteSpace(path) ? "Untitled scene" : Path.GetFileName(path);
    private static string FormatSize(ulong bytes) => bytes < 1024 ? $"{bytes} B" : bytes < 1024 * 1024 ? $"{bytes / 1024.0:F1} KB" : $"{bytes / (1024.0 * 1024.0):F1} MB";

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _refreshTimer.Stop();
        _refreshTimer.Tick -= OnRefreshTick;
        _host.EngineReady -= OnEngineReady;
        _host.StatusChanged -= OnStatusChanged;
    }
}
