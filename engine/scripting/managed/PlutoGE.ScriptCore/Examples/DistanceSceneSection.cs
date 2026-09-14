using System.Numerics;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>Attach to a persistent marker; assign an observer and section asset.
/// Loads inside radius and unloads outside radius+hysteresis.</summary>
public sealed class DistanceSceneSection : ScriptBehaviour
{
    [SerializedField] private GameObject? observer = null;
    [SerializedField] private string sceneAsset = "";
    [SerializedField] private float radius = 100;
    [SerializedField] private float hysteresis = 20;
    private SceneSection? _section;
    private bool _failed;
    public override void OnUpdate(float deltaTime)
    {
        if (observer is null || deltaTime <= 0 || !float.IsFinite(radius) || !float.IsFinite(hysteresis)) return;
        float distance = Vector3.Distance(observer.WorldPosition, GameObject.WorldPosition);
        float enter = Math.Max(0, radius), exit = enter + Math.Max(0, hysteresis);
        if (distance > exit)
        {
            Release();
            _failed = false;
        }
        else if (_section is null && !_failed && distance <= enter)
        {
            _section = SceneManager.LoadAdditive(sceneAsset);
            _failed = _section is null;
            if (_failed) Debug.LogWarning($"Section request rejected: {sceneAsset}");
        }
        if (_section?.Status is { State: SceneSectionState.Failed } status)
        {
            Debug.LogWarning($"Section load failed: {status.Error}");
            Release();
            _failed = true; // Retry only after leaving the volume.
        }
    }
    private void Release()
    {
        if (_section is null) return;
        if (_section.Status.State == SceneSectionState.Active) _section.Unload(); else _section.Cancel();
        _section.Forget();
        _section = null;
    }
    public override void OnDestroy() => Release();
}
