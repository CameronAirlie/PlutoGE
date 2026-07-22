using PlutoGE.Editor.Avalonia.Native;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class ViewportViewModel : ObservableObject
{
    private readonly EngineHost _host;
    private ulong _viewportHandle;
    private int _gizmoOperation;
    private uint _selectedEntityId;
    private float _continuousYawDegreesPerSecond;
    private string _frameStats = "Waiting for viewport";

    public ViewportViewModel(EngineHost host, SceneCameraSettingsViewModel settings)
    {
        _host = host;
        Settings = settings;
    }

    public event Action<uint>? EntityPicked;
    internal event Action? TransformManipulated;

    internal EngineHost Host => _host;
    internal SceneCameraSettingsViewModel Settings { get; }
    internal ulong ViewportHandle => _viewportHandle;
    internal uint SelectedEntityId => _selectedEntityId;

    public float ContinuousYawDegreesPerSecond
    {
        get => _continuousYawDegreesPerSecond;
        set => SetProperty(ref _continuousYawDegreesPerSecond, value);
    }

    public string FrameStats
    {
        get => _frameStats;
        private set => SetProperty(ref _frameStats, value);
    }

    internal void Attach(ulong viewportHandle)
    {
        _viewportHandle = viewportHandle;
        if (_gizmoOperation != 0)
        {
            _host.SetGizmoOperation(_viewportHandle, _gizmoOperation);
        }
    }

    internal void Detach(ulong viewportHandle)
    {
        if (_viewportHandle == viewportHandle)
        {
            _viewportHandle = 0;
            FrameStats = "Waiting for viewport";
        }
    }

    public void SetGizmoOperation(int operation)
    {
        _gizmoOperation = operation;
        if (_viewportHandle != 0)
        {
            _host.SetGizmoOperation(_viewportHandle, operation);
        }
    }

    public void SelectEntity(uint entityId) => _selectedEntityId = entityId;

    internal void PickEntity(float mouseX, float mouseY)
    {
        if (_viewportHandle == 0 || !_host.TryPickEntity(_viewportHandle, mouseX, mouseY, out var entityId))
        {
            return;
        }

        _selectedEntityId = entityId;
        EntityPicked?.Invoke(entityId);
    }

    internal void NotifyTransformManipulated() => TransformManipulated?.Invoke();

    public void RefreshStats()
    {
        if (_viewportHandle == 0 || _host.GetStats(_viewportHandle) is not { } stats)
        {
            FrameStats = "Waiting for viewport";
            return;
        }

        var hz = stats.AverageFrameMs > 0.001 ? 1000.0 / stats.AverageFrameMs : 0.0;
        FrameStats = $"{stats.AverageFrameMs:F2} ms  ·  {hz:F0} Hz  ·  {stats.Width} × {stats.Height}";
    }
}
