using System.Text.Json;

namespace PlutoGE.ScriptCore;

/// <summary>Safe file access scoped to project assets or per-user project data.</summary>
public static class ProjectStorage
{
    public static string GetAssetPath(string relativePath) =>
        ResolvePath(Application.AssetsPath, relativePath);
    public static string GetUserDataPath(string relativePath) =>
        ResolvePath(Application.PersistentDataPath, relativePath);

    public static bool AssetFileExists(string relativePath) => File.Exists(GetAssetPath(relativePath));
    public static bool UserDataFileExists(string relativePath) => File.Exists(GetUserDataPath(relativePath));
    public static string ReadAssetText(string relativePath) => File.ReadAllText(GetAssetPath(relativePath));
    public static string ReadUserDataText(string relativePath) => File.ReadAllText(GetUserDataPath(relativePath));
    public static byte[] ReadAssetBytes(string relativePath) => File.ReadAllBytes(GetAssetPath(relativePath));
    public static byte[] ReadUserDataBytes(string relativePath) => File.ReadAllBytes(GetUserDataPath(relativePath));

    public static T? ReadUserDataJson<T>(string relativePath, JsonSerializerOptions? options = null) =>
        JsonSerializer.Deserialize<T>(ReadUserDataText(relativePath), options);
    public static T? ReadAssetJson<T>(string relativePath, JsonSerializerOptions? options = null) =>
        JsonSerializer.Deserialize<T>(ReadAssetText(relativePath), options);

    public static void WriteAssetText(string relativePath, string contents) =>
        WriteTextAtomically(GetAssetPath(relativePath), contents);
    public static void WriteUserDataText(string relativePath, string contents) =>
        WriteTextAtomically(GetUserDataPath(relativePath), contents);
    public static void WriteAssetBytes(string relativePath, ReadOnlySpan<byte> contents) =>
        WriteBytesAtomically(GetAssetPath(relativePath), contents);
    public static void WriteUserDataBytes(string relativePath, ReadOnlySpan<byte> contents) =>
        WriteBytesAtomically(GetUserDataPath(relativePath), contents);
    public static void WriteAssetJson<T>(string relativePath, T value, JsonSerializerOptions? options = null) =>
        WriteAssetText(relativePath, JsonSerializer.Serialize(value, options));
    public static void WriteUserDataJson<T>(string relativePath, T value, JsonSerializerOptions? options = null) =>
        WriteUserDataText(relativePath, JsonSerializer.Serialize(value, options));

    private static string ResolvePath(string root, string relativePath)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(relativePath);
        if (Path.IsPathRooted(relativePath))
            throw new ArgumentException("Storage paths must be relative.", nameof(relativePath));

        var fullRoot = Path.GetFullPath(root);
        var fullPath = Path.GetFullPath(relativePath, fullRoot);
        var rootPrefix = Path.TrimEndingDirectorySeparator(fullRoot) + Path.DirectorySeparatorChar;
        if (!fullPath.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException(
                "Storage path cannot leave its project storage directory.", nameof(relativePath));
        return fullPath;
    }

    private static void WriteTextAtomically(string destinationPath, string contents)
    {
        ArgumentNullException.ThrowIfNull(contents);
        WriteAtomically(destinationPath, temporaryPath => File.WriteAllText(temporaryPath, contents));
    }

    private static void WriteBytesAtomically(string destinationPath, ReadOnlySpan<byte> contents)
    {
        var data = contents.ToArray();
        WriteAtomically(destinationPath, temporaryPath => File.WriteAllBytes(temporaryPath, data));
    }

    private static void WriteAtomically(string destinationPath, Action<string> writeTemporaryFile)
    {
        var directory = Path.GetDirectoryName(destinationPath)
            ?? throw new ArgumentException("The destination has no parent directory.", nameof(destinationPath));
        Directory.CreateDirectory(directory);
        var temporaryPath = Path.Combine(directory,
            $".{Path.GetFileName(destinationPath)}.{Guid.NewGuid():N}.tmp");
        try
        {
            writeTemporaryFile(temporaryPath);
            File.Move(temporaryPath, destinationPath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporaryPath))
                File.Delete(temporaryPath);
        }
    }
}
