using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Loads project scenes during Play mode or in a standalone game.</summary>
public static class SceneManager
{
    /// <summary>Opaque runtime scene generation; zero outside runtime.</summary>
    public static ulong RuntimeGeneration
    {
        get
        {
            var request = new ScriptBridge.NativeSceneStreamingRequest { Operation = 6 };
            return ScriptBridge.SceneStreaming(ref request, null, out _) ? request.Generation : 0;
        }
    }
    /// <summary>Reads a scene asynchronously and activates it on the game thread.
    /// The persistent scene's environment remains active. Returns null when the request is rejected.</summary>
    public static SceneSection? LoadAdditive(string sceneAssetReference, bool activateWhenReady = true)
    {
        if (string.IsNullOrWhiteSpace(sceneAssetReference)) return null;
        var request = new ScriptBridge.NativeSceneStreamingRequest { Operation = 0, State = activateWhenReady ? 1 : 0 };
        return ScriptBridge.SceneStreaming(ref request, sceneAssetReference, out _)
            ? new SceneSection(request.Id, request.Generation) : null;
    }
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
