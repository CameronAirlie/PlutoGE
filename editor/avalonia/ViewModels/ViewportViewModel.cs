using PlutoGE.Editor.Avalonia.Native;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class ViewportViewModel : ObservableObject
{
    private const int PerformanceSampleCapacity = 240;

    private readonly EngineHost _host;
    private readonly object _performanceSync = new();
    private readonly double[] _frameIntervalSamples = new double[PerformanceSampleCapacity];
    private readonly double[] _hostRenderSamples = new double[PerformanceSampleCapacity];
    private readonly double[] _editorRenderSamples = new double[PerformanceSampleCapacity];
    private ulong _viewportHandle;
    private PlutoNative.FrameStats? _nativeStats;
    private int _nextPerformanceSample;
    private int _performanceSampleCount;
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
            _nativeStats = null;
            FrameStats = "Waiting for viewport";
        }
    }

    internal void RecordPerformance(double frameIntervalMs, double hostRenderMs, double editorRenderMs)
    {
        lock (_performanceSync)
        {
            _frameIntervalSamples[_nextPerformanceSample] = Math.Max(0.0, frameIntervalMs);
            _hostRenderSamples[_nextPerformanceSample] = Math.Max(0.0, hostRenderMs);
            _editorRenderSamples[_nextPerformanceSample] = Math.Max(0.0, editorRenderMs);
            _nextPerformanceSample = (_nextPerformanceSample + 1) % PerformanceSampleCapacity;
            _performanceSampleCount = Math.Min(_performanceSampleCount + 1, PerformanceSampleCapacity);
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
            _nativeStats = null;
            FrameStats = "Waiting for viewport";
            return;
        }

        _nativeStats = stats;
        var hz = stats.AverageFrameMs > 0.001 ? 1000.0 / stats.AverageFrameMs : 0.0;
        FrameStats = $"{stats.AverageFrameMs:F2} ms  ·  {hz:F0} Hz  ·  {stats.Width} × {stats.Height}";
    }

    internal ViewportPerformanceSnapshot GetPerformanceSnapshot()
    {
        lock (_performanceSync)
        {
            var sampleCount = _performanceSampleCount;
            if (sampleCount == 0)
                return new ViewportPerformanceSnapshot(_viewportHandle != 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    _nativeStats?.FrameCount ?? 0, _nativeStats?.ResizeMs ?? 0.0,
                    _nativeStats?.Width ?? 0, _nativeStats?.Height ?? 0, _nativeStats?.TargetRefreshHz ?? 0.0f);

            var latestIndex = (_nextPerformanceSample + PerformanceSampleCapacity - 1) % PerformanceSampleCapacity;
            double frameTotal = 0.0;
            double hostTotal = 0.0;
            double editorTotal = 0.0;
            double frameMaximum = 0.0;
            double hostMaximum = 0.0;
            double editorMaximum = 0.0;
            for (var index = 0; index < sampleCount; ++index)
            {
                frameTotal += _frameIntervalSamples[index];
                hostTotal += _hostRenderSamples[index];
                editorTotal += _editorRenderSamples[index];
                frameMaximum = Math.Max(frameMaximum, _frameIntervalSamples[index]);
                hostMaximum = Math.Max(hostMaximum, _hostRenderSamples[index]);
                editorMaximum = Math.Max(editorMaximum, _editorRenderSamples[index]);
            }

            return new ViewportPerformanceSnapshot(
                _viewportHandle != 0,
                sampleCount,
                _frameIntervalSamples[latestIndex],
                frameTotal / sampleCount,
                frameMaximum,
                _hostRenderSamples[latestIndex],
                hostTotal / sampleCount,
                hostMaximum,
                _editorRenderSamples[latestIndex],
                editorTotal / sampleCount,
                editorMaximum,
                _nativeStats?.FrameCount ?? 0,
                _nativeStats?.ResizeMs ?? 0.0,
                _nativeStats?.Width ?? 0,
                _nativeStats?.Height ?? 0,
                _nativeStats?.TargetRefreshHz ?? 0.0f);
        }
    }
}

internal readonly record struct ViewportPerformanceSnapshot(
    bool IsHostReady,
    int SampleCount,
    double CurrentFrameIntervalMs,
    double AverageFrameIntervalMs,
    double MaximumFrameIntervalMs,
    double CurrentHostRenderMs,
    double AverageHostRenderMs,
    double MaximumHostRenderMs,
    double CurrentEditorRenderMs,
    double AverageEditorRenderMs,
    double MaximumEditorRenderMs,
    ulong FrameCount,
    double LastResizeMs,
    int Width,
    int Height,
    float TargetRefreshHz);
