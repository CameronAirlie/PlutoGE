using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public enum SceneSectionState { Reading, Ready, Active, Cancelled, Failed, Unloaded }
public readonly record struct SceneSectionStatus(SceneSectionState State, float Progress, string Error);

/// <summary>A scene-lifetime-scoped additive loading request. Use on the game thread.</summary>
public sealed class SceneSection
{
    private readonly ulong _generation;
    public ulong Id { get; }
    internal SceneSection(ulong id, ulong generation) { Id = id; _generation = generation; }
    public SceneSectionStatus Status
    {
        get
        {
            var request = new ScriptBridge.NativeSceneStreamingRequest { Id = Id, Generation = _generation, Operation = 1 };
            return ScriptBridge.SceneStreaming(ref request, null, out var error)
                ? new((SceneSectionState)request.State, request.Progress, error)
                : new(SceneSectionState.Failed, 0, error);
        }
    }
    public bool Activate() => Control(2);
    public bool Cancel() => Control(3);
    public bool Unload() => Control(4);
    /// <summary>Releases a completed request record. Active sections must be unloaded first.</summary>
    public bool Forget() => Control(5);
    private bool Control(int operation)
    {
        var request = new ScriptBridge.NativeSceneStreamingRequest { Id = Id, Generation = _generation, Operation = operation };
        return ScriptBridge.SceneStreaming(ref request, null, out _);
    }
}
