namespace PlutoGE.ScriptCore;

/// <summary>Application window, lifecycle, and project paths.</summary>
public static class Application
{
    /// <summary>True between the start and end of a play session (editor Play, or a running game).</summary>
    public static bool IsPlaying { get; private set; }

    /// <summary>
    /// Raised when play starts, before any script's OnCreate, after [ResetOnPlay] statics are reset. Use it to give
    /// static state its starting values.
    /// </summary>
    public static event Action? PlayStarted;

    /// <summary>Raised when play stops, after every script's OnDestroy. Static state survives into the next session
    /// unless reset here or marked [ResetOnPlay].</summary>
    public static event Action? PlayStopped;

    internal static void SetPlaying(bool playing) => IsPlaying = playing;
    internal static void RaisePlayStarted() => Raise(PlayStarted, nameof(PlayStarted));
    internal static void RaisePlayStopped() => Raise(PlayStopped, nameof(PlayStopped));

    // One failing handler does not stop the others.
    private static void Raise(Action? handlers, string name)
    {
        if (handlers is null)
            return;
        foreach (var handler in handlers.GetInvocationList().Cast<Action>())
        {
            try
            {
                handler();
            }
            catch (Exception exception)
            {
                Debug.LogError($"Application.{name} handler {handler.Method.DeclaringType?.Name}.{handler.Method.Name} threw: {exception}");
            }
        }
    }

    public static bool WindowSizeSupported => Native.ScriptBridge.WindowSizeSupported;
    /// <summary>Logical client size; in borderless mode returns the windowed restore size.</summary>
    public static (int Width, int Height) WindowedSize => Native.ScriptBridge.GetWindowedSize(false);
    /// <summary>Maximum windowed client size fitting the current monitor work area, with decoration margin.</summary>
    public static (int Width, int Height) WindowedSizeLimit => Native.ScriptBridge.GetWindowedSize(true);
    /// <summary>Resizes the host window, or its borderless restore size. Invalid/out-of-bounds sizes are rejected.</summary>
    public static bool TrySetWindowedSize(int width, int height) => Native.ScriptBridge.SetWindowedSize(width, height);
    /// <summary>Borderless fullscreen on the window's current monitor. Changes are ignored in editor hosts.</summary>
    public static bool Fullscreen
    {
        get => Native.ScriptBridge.GetWindowFullscreen();
        set => Native.ScriptBridge.SetWindowFullscreen(value);
    }

    private static readonly object Gate = new();
    // Keep type initialization free of filesystem and environment lookups. The
    // script bridge configures both paths before exposing any script classes.
    // If one of those lookups fails, callers now receive the useful underlying
    // exception instead of a permanently cached TypeInitializationException.
    private static string _assetsPath = AppContext.BaseDirectory;
    private static string _persistentDataPath = AppContext.BaseDirectory;

    /// <summary>Absolute path to the project's Assets directory.</summary>
    public static string AssetsPath
    {
        get { lock (Gate) return _assetsPath; }
    }

    /// <summary>Absolute per-user path for data belonging to the current project.</summary>
    public static string PersistentDataPath
    {
        get { lock (Gate) return _persistentDataPath; }
    }

    /// <summary>Stops play mode in the editor; closes the application in a built project.</summary>
    public static void Quit()
    {
        Native.ScriptBridge.QuitApplication();
    }

    internal static void ConfigureForScriptAssembly(string assemblyPath, string projectName)
    {
        var assemblyDirectory = Path.GetDirectoryName(Path.GetFullPath(assemblyPath))
            ?? AppContext.BaseDirectory;
        var assetsPath = string.Equals(Path.GetFileName(assemblyDirectory), "Managed",
            StringComparison.OrdinalIgnoreCase)
                ? Directory.GetParent(assemblyDirectory)?.FullName ?? assemblyDirectory
                : assemblyDirectory;

        lock (Gate)
        {
            _assetsPath = Path.GetFullPath(assetsPath);
            _persistentDataPath = BuildPersistentDataPath(projectName);
        }
    }

    private static string BuildPersistentDataPath(string projectName)
    {
        var root = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (string.IsNullOrWhiteSpace(root))
            root = AppContext.BaseDirectory;

        var invalid = Path.GetInvalidFileNameChars();
        var safeName = new string(projectName
            .Select(character => invalid.Contains(character) ? '_' : character)
            .ToArray()).Trim();
        if (string.IsNullOrWhiteSpace(safeName))
            safeName = "Project";

        return Path.GetFullPath(Path.Combine(root, "PlutoGE", safeName));
    }
}
