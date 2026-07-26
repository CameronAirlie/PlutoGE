namespace PlutoGE.ScriptCore;

/// <summary>Paths associated with the currently loaded PlutoGE project.</summary>
public static class Application
{
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
