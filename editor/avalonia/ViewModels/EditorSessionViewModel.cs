using System.Collections.ObjectModel;
using System.Diagnostics;
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
    private AssetItemViewModel? _selectedAsset;
    private AssetFolderViewModel? _selectedAssetFolder;
    private string _searchText = string.Empty;
    private string _assetSearchText = string.Empty;
    private string _currentAssetFolder = string.Empty;
    private string _statusText = "Open the Viewport to start PlutoGE";
    private string _engineStatusText = "ENGINE WAITING";
    private string _projectName = "No project loaded";
    private string _projectPath = string.Empty;
    private string _assetDirectory = string.Empty;
    private string _activeScene = "Untitled scene";
    private bool _isPlaying;
    private bool _disposed;

    private readonly Func<Task> _saveSceneAs;

    internal EditorSessionViewModel(
        EngineHost host,
        Func<Task> openProject,
        Func<Task> openScene,
        Func<Task> saveSceneAs,
        Func<Task> openProjectSettings)
    {
        _host = host;
        _saveSceneAs = saveSceneAs;
        SceneCamera = new SceneCameraSettingsViewModel(host);
        Inspector = new TransformInspectorViewModel(host);
        OpenProjectCommand = new AsyncRelayCommand(openProject, () => !IsPlaying);
        OpenSceneCommand = new AsyncRelayCommand(openScene, () => !IsPlaying);
        NewSceneCommand = new AsyncRelayCommand(NewSceneAsync, () => _host.IsReady && !IsPlaying);
        OpenSelectedSceneCommand = new RelayCommand(OpenSelectedScene, () => SelectedScene is not null);
        SaveProjectCommand = new RelayCommand(SaveProject, () => HasProject && !IsPlaying);
        SaveSceneCommand = new AsyncRelayCommand(SaveSceneAsync, () => _host.IsReady && !IsPlaying);
        SaveSceneAsCommand = new AsyncRelayCommand(saveSceneAs, () => _host.IsReady && !IsPlaying);
        OpenProjectSettingsCommand = new AsyncRelayCommand(openProjectSettings, () => HasProject && !IsPlaying);
        PlayCommand = new AsyncRelayCommand(StartPlayingAsync, () => _host.IsReady && !IsPlaying);
        StopCommand = new AsyncRelayCommand(StopPlayingAsync, () => IsPlaying);
        CreateRootEntityCommand = new AsyncRelayCommand(() => CreateEntityAsync(0), () => _host.IsReady && !IsPlaying);
        CreateChildEntityCommand = new AsyncRelayCommand(
            () => CreateEntityAsync(SelectedEntity?.Id ?? 0), () => SelectedEntity is not null && !IsPlaying);
        DuplicateEntityCommand = new AsyncRelayCommand(DuplicateSelectedEntityAsync, () => SelectedEntity is not null && !IsPlaying);
        DeleteEntityCommand = new AsyncRelayCommand(DeleteSelectedEntityAsync, () => SelectedEntity is not null && !IsPlaying);
        ToggleEntityActiveCommand = new RelayCommand(ToggleSelectedEntity, () => SelectedEntity is not null && !IsPlaying);
        RefreshAssetsCommand = new RelayCommand(RefreshAssets, () => HasProject);
        OpenSelectedAssetCommand = new AsyncRelayCommand(OpenSelectedAssetAsync, () => SelectedAsset is not null);
        RevealSelectedAssetCommand = new RelayCommand(RevealSelectedAsset, () => SelectedAsset is not null);
        NavigateAssetFolderUpCommand = new RelayCommand(NavigateAssetFolderUp, () => !string.IsNullOrEmpty(CurrentAssetFolder));
        CreateAssetFolderCommand = new RelayCommand(CreateAssetFolder, () => HasProject && !IsPlaying);

        _host.EngineReady += OnEngineReady;
        _host.StatusChanged += OnStatusChanged;
        _refreshTimer = new DispatcherTimer(DispatcherPriority.Background)
        {
            Interval = TimeSpan.FromMilliseconds(500),
        };
        _refreshTimer.Tick += OnRefreshTick;
        _refreshTimer.Start();
    }

    internal event Action<uint>? SelectionChanged;
    internal EngineHost Host => _host;

    public ObservableCollection<EntityNodeViewModel> Hierarchy { get; } = [];
    public ObservableCollection<AssetItemViewModel> Assets { get; } = [];
    public ObservableCollection<AssetItemViewModel> Scenes { get; } = [];
    public ObservableCollection<AssetFolderViewModel> AssetFolders { get; } = [];
    public ObservableCollection<AssetItemViewModel> BrowserItems { get; } = [];
    public TransformInspectorViewModel Inspector { get; }
    public SceneCameraSettingsViewModel SceneCamera { get; }
    public AsyncRelayCommand OpenProjectCommand { get; }
    public AsyncRelayCommand OpenSceneCommand { get; }
    public AsyncRelayCommand NewSceneCommand { get; }
    public RelayCommand OpenSelectedSceneCommand { get; }
    public ICommand SaveProjectCommand { get; }
    public AsyncRelayCommand SaveSceneCommand { get; }
    public AsyncRelayCommand SaveSceneAsCommand { get; }
    public AsyncRelayCommand OpenProjectSettingsCommand { get; }
    public AsyncRelayCommand PlayCommand { get; }
    public AsyncRelayCommand StopCommand { get; }
    public AsyncRelayCommand CreateRootEntityCommand { get; }
    public AsyncRelayCommand CreateChildEntityCommand { get; }
    public AsyncRelayCommand DuplicateEntityCommand { get; }
    public AsyncRelayCommand DeleteEntityCommand { get; }
    public ICommand ToggleEntityActiveCommand { get; }
    public ICommand RefreshAssetsCommand { get; }
    public AsyncRelayCommand OpenSelectedAssetCommand { get; }
    public ICommand RevealSelectedAssetCommand { get; }
    public ICommand NavigateAssetFolderUpCommand { get; }
    public ICommand CreateAssetFolderCommand { get; }

    public EntityNodeViewModel? SelectedEntity
    {
        get => _selectedEntity;
        set
        {
            if (!SetProperty(ref _selectedEntity, value)) return;
            Inspector.Select(value);
            SelectionChanged?.Invoke(value?.Id ?? 0);
            RaiseCommandStates();
        }
    }

    public AssetItemViewModel? SelectedAsset
    {
        get => _selectedAsset;
        set
        {
            if (!SetProperty(ref _selectedAsset, value)) return;
            OpenSelectedAssetCommand.RaiseCanExecuteChanged();
            if (RevealSelectedAssetCommand is RelayCommand reveal) reveal.RaiseCanExecuteChanged();
        }
    }

    public AssetFolderViewModel? SelectedAssetFolder
    {
        get => _selectedAssetFolder;
        set
        {
            if (!SetProperty(ref _selectedAssetFolder, value) || value is null) return;
            CurrentAssetFolder = value.Path;
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

    public string AssetSearchText
    {
        get => _assetSearchText;
        set
        {
            if (SetProperty(ref _assetSearchText, value ?? string.Empty)) RebuildAssetBrowser();
        }
    }

    public string CurrentAssetFolder
    {
        get => _currentAssetFolder;
        private set
        {
            if (!SetProperty(ref _currentAssetFolder, value.Replace('\\', '/').Trim('/'))) return;
            OnPropertyChanged(nameof(AssetBreadcrumb));
            RebuildAssetBrowser();
            if (NavigateAssetFolderUpCommand is RelayCommand up) up.RaiseCanExecuteChanged();
        }
    }

    public string AssetBreadcrumb => string.IsNullOrEmpty(CurrentAssetFolder)
        ? "Assets"
        : $"Assets / {CurrentAssetFolder.Replace("/", " / ")}";

    public string StatusText { get => _statusText; private set => SetProperty(ref _statusText, value); }
    public string EngineStatusText { get => _engineStatusText; private set => SetProperty(ref _engineStatusText, value); }
    public string ProjectName { get => _projectName; private set => SetProperty(ref _projectName, value); }
    public string ProjectPath { get => _projectPath; private set => SetProperty(ref _projectPath, value); }
    public string AssetDirectory { get => _assetDirectory; private set => SetProperty(ref _assetDirectory, value); }
    public string ActiveScene { get => _activeScene; private set => SetProperty(ref _activeScene, value); }
    public bool HasProject => !string.IsNullOrWhiteSpace(ProjectPath);
    public bool IsPlaying
    {
        get => _isPlaying;
        private set
        {
            if (!SetProperty(ref _isPlaying, value)) return;
            OnPropertyChanged(nameof(PlayStateText));
            EngineStatusText = value ? "PLAY MODE" : "ENGINE LIVE";
            RaiseCommandStates();
        }
    }
    public string PlayStateText => IsPlaying ? "PLAYING" : "EDIT MODE";

    internal async Task LoadProjectAsync(string path)
    {
        try
        {
            ApplyProject(await _host.LoadProjectAsync(path));
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

    internal async Task LoadSceneAsync(string pathOrReference)
    {
        try
        {
            ActiveScene = FormatSceneName(await _host.LoadSceneAsync(pathOrReference));
            LoadHierarchy();
            StatusText = $"Opened scene: {ActiveScene}";
        }
        catch (Exception exception)
        {
            ReportError(exception.Message);
        }
    }

    private async Task NewSceneAsync()
    {
        try
        {
            await _host.NewSceneAsync();
            ActiveScene = "Untitled scene";
            LoadHierarchy();
            StatusText = "Created a new scene";
        }
        catch (Exception exception) { ReportError(exception.Message); }
    }

    internal async Task SaveSceneToAsync(string path)
    {
        try
        {
            ActiveScene = FormatSceneName(_host.SaveScene(path));
            StatusText = $"Saved scene: {ActiveScene}";
            if (HasProject) ApplyProject(_host.SaveProject());
        }
        catch (Exception exception)
        {
            ReportError(exception.Message);
        }
        await Task.CompletedTask;
    }

    internal void ApplyProjectSettings(ProjectSettingsDocument settings)
    {
        try
        {
            _host.WriteProjectSettings(settings);
            ApplyProject(_host.SaveProject());
            StatusText = "Saved project settings";
        }
        catch (Exception exception)
        {
            ReportError(exception.Message);
        }
    }

    internal ProjectSettingsDocument ReadProjectSettings() => _host.ReadProjectSettings();

    internal async Task ActivateBrowserItemAsync(AssetItemViewModel item)
    {
        SelectedAsset = item;
        if (item.IsFolder)
        {
            CurrentAssetFolder = item.RelativePath;
            return;
        }
        if (item.IsScene)
        {
            await LoadSceneAsync(item.Reference);
            return;
        }
        OpenPath(item.AbsolutePath);
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

    private async void OpenSelectedScene()
    {
        if (SelectedScene is { } scene) await LoadSceneAsync(scene.Reference);
    }

    private async Task SaveSceneAsync()
    {
        var scenePath = _host.ReadScenePath();
        if (string.IsNullOrWhiteSpace(scenePath))
        {
            await _saveSceneAs();
            return;
        }
        await SaveSceneToAsync(scenePath);
    }

    private void SaveProject()
    {
        try
        {
            ApplyProject(_host.SaveProject());
            ActiveScene = FormatSceneName(_host.ReadScenePath());
            StatusText = $"Saved project: {ProjectName}";
        }
        catch (Exception exception)
        {
            ReportError(exception.Message);
        }
    }

    private async Task StartPlayingAsync()
    {
        try
        {
            await _host.StartRuntimeAsync();
            IsPlaying = true;
            StatusText = "Entered Play mode (scene changes will be restored on Stop)";
        }
        catch (Exception exception) { ReportError(exception.Message); }
    }

    private async Task StopPlayingAsync()
    {
        try
        {
            await _host.StopRuntimeAsync();
            IsPlaying = false;
            LoadHierarchy();
            SceneCamera.RefreshPostProcessing();
            StatusText = "Stopped Play mode and restored the scene";
        }
        catch (Exception exception) { ReportError(exception.Message); }
    }

    private async Task CreateEntityAsync(uint parentId)
    {
        try
        {
            var entityId = await _host.CreateEntityAsync(parentId, "GameObject");
            LoadHierarchy();
            SelectEntity(entityId);
            StatusText = parentId == 0 ? "Created GameObject" : "Created child GameObject";
        }
        catch (Exception exception) { ReportError(exception.Message); }
    }

    private async Task DuplicateSelectedEntityAsync()
    {
        if (SelectedEntity is not { } selected) return;
        try
        {
            var duplicateId = await _host.DuplicateEntityAsync(selected.Id);
            LoadHierarchy();
            SelectEntity(duplicateId);
            StatusText = $"Duplicated {selected.Name}";
        }
        catch (Exception exception) { ReportError(exception.Message); }
    }

    private async Task DeleteSelectedEntityAsync()
    {
        if (SelectedEntity is not { } selected) return;
        try
        {
            await _host.DeleteEntityAsync(selected.Id);
            SelectedEntity = null;
            LoadHierarchy();
            StatusText = $"Deleted {selected.Name}";
        }
        catch (Exception exception) { ReportError(exception.Message); }
    }

    private void ToggleSelectedEntity()
    {
        if (SelectedEntity is null) return;
        Inspector.IsActive = !Inspector.IsActive;
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
        RaiseCommandStates();
    });

    private void OnStatusChanged(object? sender, string status) => Dispatcher.UIThread.Post(() => ReportError(status));

    private void ApplyProject(ProjectDocument project)
    {
        ProjectName = project.Name;
        ProjectPath = project.ManifestPath;
        AssetDirectory = project.AssetDirectory;
        OnPropertyChanged(nameof(HasProject));
        Assets.Clear();
        Scenes.Clear();
        foreach (var asset in project.Assets)
        {
            var relativePath = asset.Reference.StartsWith("project://", StringComparison.OrdinalIgnoreCase)
                ? asset.Reference["project://".Length..]
                : asset.Reference;
            var item = new AssetItemViewModel(asset.Reference, asset.Type, asset.IsScene, FormatSize(asset.Size),
                Path.Combine(project.AssetDirectory, relativePath.Replace('/', Path.DirectorySeparatorChar)));
            Assets.Add(item);
            if (item.IsScene) Scenes.Add(item);
        }
        SelectedScene = null;
        BuildAssetFolders();
        RebuildAssetBrowser();
        RaiseCommandStates();
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
        return matches || children.Length > 0 ? new EntityNodeViewModel(node.Id, node.Name, node.Active, children) : null;
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
        if (_host.IsRuntimeRunning != IsPlaying) IsPlaying = _host.IsRuntimeRunning;
        Inspector.RefreshFromNative(refreshComponents: Inspector.HasSelection && Inspector.Components.Count == 0);
        if (!SceneCamera.PostProcessingLoaded)
            SceneCamera.RefreshPostProcessing();
    }

    private void RefreshAssets()
    {
        try
        {
            ApplyProject(_host.RefreshProject());
            StatusText = "Refreshed project assets";
        }
        catch (Exception exception) { ReportError(exception.Message); }
    }

    private async Task OpenSelectedAssetAsync()
    {
        if (SelectedAsset is not null) await ActivateBrowserItemAsync(SelectedAsset);
    }

    private void RevealSelectedAsset()
    {
        if (SelectedAsset is null) return;
        var path = SelectedAsset.IsFolder
            ? Path.Combine(AssetDirectory, SelectedAsset.RelativePath.Replace('/', Path.DirectorySeparatorChar))
            : SelectedAsset.AbsolutePath;
        try
        {
            if (OperatingSystem.IsWindows())
            {
                var arguments = File.Exists(path) ? $"/select,\"{path}\"" : $"\"{path}\"";
                Process.Start(new ProcessStartInfo("explorer.exe", arguments) { UseShellExecute = true });
            }
            else
            {
                OpenPath(File.Exists(path) ? Path.GetDirectoryName(path) ?? path : path);
            }
        }
        catch (Exception exception) { ReportError($"Could not reveal asset: {exception.Message}"); }
    }

    private void NavigateAssetFolderUp()
    {
        if (string.IsNullOrEmpty(CurrentAssetFolder)) return;
        CurrentAssetFolder = (Path.GetDirectoryName(CurrentAssetFolder) ?? string.Empty).Replace('\\', '/');
    }

    private void CreateAssetFolder()
    {
        try
        {
            var parent = Path.Combine(AssetDirectory, CurrentAssetFolder.Replace('/', Path.DirectorySeparatorChar));
            var candidate = Path.Combine(parent, "New Folder");
            for (var suffix = 2; Directory.Exists(candidate); ++suffix)
                candidate = Path.Combine(parent, $"New Folder {suffix}");
            Directory.CreateDirectory(candidate);
            RefreshAssets();
            StatusText = $"Created folder: {Path.GetFileName(candidate)}";
        }
        catch (Exception exception) { ReportError(exception.Message); }
    }

    private void BuildAssetFolders()
    {
        AssetFolders.Clear();
        var root = new AssetFolderViewModel("Assets", string.Empty);
        AssetFolders.Add(root);
        var folderPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var asset in Assets)
        {
            var folder = asset.FolderPath;
            while (!string.IsNullOrEmpty(folder))
            {
                folderPaths.Add(folder.Replace('\\', '/'));
                folder = (Path.GetDirectoryName(folder) ?? string.Empty).Replace('\\', '/');
            }
        }
        if (Directory.Exists(AssetDirectory))
        {
            try
            {
                foreach (var directory in Directory.EnumerateDirectories(AssetDirectory, "*", SearchOption.AllDirectories))
                    folderPaths.Add(Path.GetRelativePath(AssetDirectory, directory).Replace('\\', '/'));
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        var nodes = new Dictionary<string, AssetFolderViewModel>(StringComparer.OrdinalIgnoreCase) { [string.Empty] = root };
        foreach (var path in folderPaths.OrderBy(path => path.Count(character => character == '/')).ThenBy(path => path))
        {
            var parentPath = (Path.GetDirectoryName(path) ?? string.Empty).Replace('\\', '/');
            if (!nodes.TryGetValue(parentPath, out var parent)) parent = root;
            var node = new AssetFolderViewModel(Path.GetFileName(path), path);
            parent.Children.Add(node);
            nodes[path] = node;
        }
        if (!string.IsNullOrEmpty(CurrentAssetFolder) && !nodes.ContainsKey(CurrentAssetFolder)) CurrentAssetFolder = string.Empty;
    }

    private void RebuildAssetBrowser()
    {
        BrowserItems.Clear();
        var search = AssetSearchText.Trim();
        if (string.IsNullOrEmpty(search))
        {
            var childFolders = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var asset in Assets)
            {
                if (!asset.RelativePath.StartsWith(string.IsNullOrEmpty(CurrentAssetFolder) ? string.Empty : CurrentAssetFolder + "/",
                                                   StringComparison.OrdinalIgnoreCase)) continue;
                var remainder = string.IsNullOrEmpty(CurrentAssetFolder)
                    ? asset.RelativePath
                    : asset.RelativePath[(CurrentAssetFolder.Length + 1)..];
                var slash = remainder.IndexOf('/');
                if (slash > 0)
                {
                    var child = string.IsNullOrEmpty(CurrentAssetFolder)
                        ? remainder[..slash]
                        : $"{CurrentAssetFolder}/{remainder[..slash]}";
                    childFolders.Add(child);
                }
            }
            var diskPath = Path.Combine(AssetDirectory, CurrentAssetFolder.Replace('/', Path.DirectorySeparatorChar));
            if (Directory.Exists(diskPath))
            {
                try
                {
                    foreach (var directory in Directory.EnumerateDirectories(diskPath))
                    {
                        var relative = Path.GetRelativePath(AssetDirectory, directory).Replace('\\', '/');
                        childFolders.Add(relative);
                    }
                }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
            foreach (var folder in childFolders.OrderBy(path => path)) BrowserItems.Add(AssetItemViewModel.Folder(folder));
            foreach (var asset in Assets.Where(asset => string.Equals(asset.FolderPath, CurrentAssetFolder, StringComparison.OrdinalIgnoreCase))
                                        .OrderBy(asset => asset.DisplayName))
                BrowserItems.Add(asset);
        }
        else
        {
            foreach (var asset in Assets.Where(asset => asset.DisplayName.Contains(search, StringComparison.OrdinalIgnoreCase) ||
                                                        asset.Type.Contains(search, StringComparison.OrdinalIgnoreCase))
                                        .OrderBy(asset => asset.DisplayName))
                BrowserItems.Add(asset);
        }
        SelectedAsset = null;
    }

    private void RaiseCommandStates()
    {
        if (SaveProjectCommand is RelayCommand saveProject) saveProject.RaiseCanExecuteChanged();
        OpenProjectCommand.RaiseCanExecuteChanged();
        OpenSceneCommand.RaiseCanExecuteChanged();
        NewSceneCommand.RaiseCanExecuteChanged();
        SaveSceneCommand.RaiseCanExecuteChanged();
        SaveSceneAsCommand.RaiseCanExecuteChanged();
        OpenProjectSettingsCommand.RaiseCanExecuteChanged();
        PlayCommand.RaiseCanExecuteChanged();
        StopCommand.RaiseCanExecuteChanged();
        CreateRootEntityCommand.RaiseCanExecuteChanged();
        CreateChildEntityCommand.RaiseCanExecuteChanged();
        DuplicateEntityCommand.RaiseCanExecuteChanged();
        DeleteEntityCommand.RaiseCanExecuteChanged();
        if (ToggleEntityActiveCommand is RelayCommand toggle) toggle.RaiseCanExecuteChanged();
        if (RefreshAssetsCommand is RelayCommand refresh) refresh.RaiseCanExecuteChanged();
        if (CreateAssetFolderCommand is RelayCommand createFolder) createFolder.RaiseCanExecuteChanged();
    }

    private static void OpenPath(string path)
    {
        if (string.IsNullOrWhiteSpace(path)) return;
        Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
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
