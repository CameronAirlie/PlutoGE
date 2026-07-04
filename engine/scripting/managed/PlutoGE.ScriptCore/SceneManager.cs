using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Loads project scenes during Play mode or in a standalone game.</summary>
public static class SceneManager
{
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
