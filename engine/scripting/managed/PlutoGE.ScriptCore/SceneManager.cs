using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Loads project scenes during Play mode or in a standalone game.</summary>
public static class SceneManager
{
    /// <summary>Returns a descriptor for the scene currently owned by the engine.</summary>
    public static Scene GetActiveScene()
    {
        return new Scene(ScriptBridge.GetActiveScenePath());
    }

    /// <summary>
    /// Requests a scene transition at the end of the current frame.
    /// Accepts a scene name ("Game"), project-relative path ("Scenes/Game.plutoscene"),
    /// or full project asset reference ("project://Scenes/Game.plutoscene").
    /// </summary>
    public static bool LoadScene(string sceneAssetReference)
    {
        return ScriptBridge.LoadScene(sceneAssetReference);
    }
}

/// <summary>A lightweight descriptor for a project scene.</summary>
public sealed class Scene
{
    internal Scene(string path)
    {
        Path = path;
        Name = System.IO.Path.GetFileNameWithoutExtension(path);
    }

    /// <summary>The scene filename without its extension.</summary>
    public string Name { get; }

    /// <summary>The active scene's asset path, or an empty string for an unsaved scene.</summary>
    public string Path { get; }
}
