using System.Collections.ObjectModel;
using System.Windows.Input;
using PlutoGE.Editor.Avalonia.Native;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class ProjectSettingsViewModel : ObservableObject
{
    private readonly EditorSessionViewModel _session;
    private string _name;
    private string _windowTitle;
    private string _startupScene;
    private string _scriptAssembly;
    private decimal _windowWidth;
    private decimal _windowHeight;
    private bool _vSyncEnabled;
    private decimal _editorFontSize;

    internal ProjectSettingsViewModel(EditorSessionViewModel session)
    {
        _session = session;
        var settings = session.ReadProjectSettings();
        _name = settings.Name;
        _windowTitle = settings.WindowTitle;
        _startupScene = settings.StartupScene;
        _scriptAssembly = settings.ScriptAssembly;
        _windowWidth = settings.WindowWidth;
        _windowHeight = settings.WindowHeight;
        _vSyncEnabled = settings.VSyncEnabled;
        _editorFontSize = (decimal)settings.EditorFontSize;
        StartupScenes.Add(string.Empty);
        foreach (var scene in session.Scenes) StartupScenes.Add(scene.Reference);
        SaveCommand = new RelayCommand(Save);
        CancelCommand = new RelayCommand(() => CloseRequested?.Invoke(false));
    }

    internal event Action<bool>? CloseRequested;
    public ObservableCollection<string> StartupScenes { get; } = [];
    public ICommand SaveCommand { get; }
    public ICommand CancelCommand { get; }
    public string ProjectPath => _session.ProjectPath;
    public string AssetDirectory => _session.AssetDirectory;

    public string Name { get => _name; set => SetProperty(ref _name, value ?? string.Empty); }
    public string WindowTitle { get => _windowTitle; set => SetProperty(ref _windowTitle, value ?? string.Empty); }
    public string StartupScene { get => _startupScene; set => SetProperty(ref _startupScene, value ?? string.Empty); }
    public string ScriptAssembly { get => _scriptAssembly; set => SetProperty(ref _scriptAssembly, value ?? string.Empty); }
    public decimal WindowWidth { get => _windowWidth; set => SetProperty(ref _windowWidth, Math.Max(64, value)); }
    public decimal WindowHeight { get => _windowHeight; set => SetProperty(ref _windowHeight, Math.Max(64, value)); }
    public bool VSyncEnabled { get => _vSyncEnabled; set => SetProperty(ref _vSyncEnabled, value); }
    public decimal EditorFontSize { get => _editorFontSize; set => SetProperty(ref _editorFontSize, Math.Clamp(value, 10m, 24m)); }

    private void Save()
    {
        _session.ApplyProjectSettings(new ProjectSettingsDocument(
            Name.Trim(), WindowTitle.Trim(), StartupScene, ScriptAssembly.Trim(),
            (int)WindowWidth, (int)WindowHeight, VSyncEnabled, (float)EditorFontSize));
        CloseRequested?.Invoke(true);
    }
}
