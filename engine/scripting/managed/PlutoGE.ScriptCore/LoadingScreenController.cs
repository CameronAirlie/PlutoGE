namespace PlutoGE.ScriptCore;

/// <summary>
/// An ownerless controller instantiated once per scene transition. Only these
/// callbacks run during loading. Use Document for UI; scene entities may be
/// replaced during any callback. RCSS animations need no controller.
/// </summary>
public abstract class LoadingScreenController : ScriptBehaviour
{
    protected RmlDocument Document { get; } = new("loading://active");
    public sealed override void OnCreate() => OnLoadingStarted();
    public sealed override void OnUpdate(float deltaTime) => OnLoadingUpdate(SceneManager.LoadingStatus, deltaTime);
    public sealed override void OnDestroy()
    {
        try { OnLoadingFinished(); }
        finally { Document.Dispose(); }
    }
    protected virtual void OnLoadingStarted() { }
    protected virtual void OnLoadingUpdate(SceneLoadStatus status, float deltaTime) { }
    protected virtual void OnLoadingFinished() { }
}
