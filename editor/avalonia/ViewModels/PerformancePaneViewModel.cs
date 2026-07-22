using System.Diagnostics;
using System.Globalization;
using System.Text;
using Avalonia.Threading;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal enum PerformanceScope
{
    Both,
    Host,
    Editor,
}

internal sealed class PerformancePaneViewModel : ObservableObject, IDisposable
{
    private readonly ViewportViewModel _viewport;
    private readonly DispatcherTimer _timer;
    private readonly Process _process = Process.GetCurrentProcess();
    private TimeSpan _previousCpuTime;
    private long _previousSampleTimestamp;
    private PerformanceScope _selectedScope;
    private string _copyStatus = string.Empty;
    private string _hostStatus = "Waiting for viewport";
    private string _currentFrameInterval = "—";
    private string _averageFrameInterval = "—";
    private string _maximumFrameInterval = "—";
    private string _frameRate = "—";
    private string _unaccountedAverage = "—";
    private string _combinedCurrent = "—";
    private string _combinedAverage = "—";
    private string _sampleCount = "0";
    private string _hostCurrent = "—";
    private string _hostAverage = "—";
    private string _hostMaximum = "—";
    private string _hostShare = "—";
    private string _hostFrameCount = "0";
    private string _viewportSize = "—";
    private string _targetRefreshRate = "—";
    private string _gpuFrameTime = "—";
    private string _lastResize = "—";
    private string _editorCurrent = "—";
    private string _editorAverage = "—";
    private string _editorMaximum = "—";
    private string _editorShare = "—";
    private string _processCpu = "—";
    private string _workingSet = "—";
    private string _privateMemory = "—";
    private string _managedMemory = "—";
    private string _threadCount = "—";
    private string _garbageCollections = "—";
    private string _uptime = "—";

    internal PerformancePaneViewModel(ViewportViewModel viewport)
    {
        _viewport = viewport;
        Scopes = Enum.GetValues<PerformanceScope>();
        _previousCpuTime = _process.TotalProcessorTime;
        _previousSampleTimestamp = Stopwatch.GetTimestamp();
        _timer = new DispatcherTimer(DispatcherPriority.Background)
        {
            Interval = TimeSpan.FromMilliseconds(500),
        };
        _timer.Tick += OnTick;
        _timer.Start();
        Refresh();
    }

    public IReadOnlyList<PerformanceScope> Scopes { get; }

    public PerformanceScope SelectedScope
    {
        get => _selectedScope;
        set
        {
            if (!SetProperty(ref _selectedScope, value)) return;
            OnPropertyChanged(nameof(ShowCombined));
            OnPropertyChanged(nameof(ShowHost));
            OnPropertyChanged(nameof(ShowEditor));
            CopyStatus = string.Empty;
        }
    }

    public bool ShowCombined => SelectedScope == PerformanceScope.Both;
    public bool ShowHost => SelectedScope != PerformanceScope.Editor;
    public bool ShowEditor => SelectedScope != PerformanceScope.Host;
    public string CopyStatus { get => _copyStatus; private set => SetProperty(ref _copyStatus, value); }
    public string HostStatus { get => _hostStatus; private set => SetProperty(ref _hostStatus, value); }
    public string CurrentFrameInterval { get => _currentFrameInterval; private set => SetProperty(ref _currentFrameInterval, value); }
    public string AverageFrameInterval { get => _averageFrameInterval; private set => SetProperty(ref _averageFrameInterval, value); }
    public string MaximumFrameInterval { get => _maximumFrameInterval; private set => SetProperty(ref _maximumFrameInterval, value); }
    public string FrameRate { get => _frameRate; private set => SetProperty(ref _frameRate, value); }
    public string UnaccountedAverage { get => _unaccountedAverage; private set => SetProperty(ref _unaccountedAverage, value); }
    public string CombinedCurrent { get => _combinedCurrent; private set => SetProperty(ref _combinedCurrent, value); }
    public string CombinedAverage { get => _combinedAverage; private set => SetProperty(ref _combinedAverage, value); }
    public string SampleCount { get => _sampleCount; private set => SetProperty(ref _sampleCount, value); }
    public string HostCurrent { get => _hostCurrent; private set => SetProperty(ref _hostCurrent, value); }
    public string HostAverage { get => _hostAverage; private set => SetProperty(ref _hostAverage, value); }
    public string HostMaximum { get => _hostMaximum; private set => SetProperty(ref _hostMaximum, value); }
    public string HostShare { get => _hostShare; private set => SetProperty(ref _hostShare, value); }
    public string HostFrameCount { get => _hostFrameCount; private set => SetProperty(ref _hostFrameCount, value); }
    public string ViewportSize { get => _viewportSize; private set => SetProperty(ref _viewportSize, value); }
    public string TargetRefreshRate { get => _targetRefreshRate; private set => SetProperty(ref _targetRefreshRate, value); }
    public string GpuFrameTime { get => _gpuFrameTime; private set => SetProperty(ref _gpuFrameTime, value); }
    public string LastResize { get => _lastResize; private set => SetProperty(ref _lastResize, value); }
    public string EditorCurrent { get => _editorCurrent; private set => SetProperty(ref _editorCurrent, value); }
    public string EditorAverage { get => _editorAverage; private set => SetProperty(ref _editorAverage, value); }
    public string EditorMaximum { get => _editorMaximum; private set => SetProperty(ref _editorMaximum, value); }
    public string EditorShare { get => _editorShare; private set => SetProperty(ref _editorShare, value); }
    public string ProcessCpu { get => _processCpu; private set => SetProperty(ref _processCpu, value); }
    public string WorkingSet { get => _workingSet; private set => SetProperty(ref _workingSet, value); }
    public string PrivateMemory { get => _privateMemory; private set => SetProperty(ref _privateMemory, value); }
    public string ManagedMemory { get => _managedMemory; private set => SetProperty(ref _managedMemory, value); }
    public string ThreadCount { get => _threadCount; private set => SetProperty(ref _threadCount, value); }
    public string GarbageCollections { get => _garbageCollections; private set => SetProperty(ref _garbageCollections, value); }
    public string Uptime { get => _uptime; private set => SetProperty(ref _uptime, value); }

    private void OnTick(object? sender, EventArgs e) => Refresh();

    private void Refresh()
    {
        _viewport.RefreshStats();
        var metrics = _viewport.GetPerformanceSnapshot();
        var hasSamples = metrics.SampleCount > 0;
        var combinedCurrentMs = metrics.CurrentHostRenderMs + metrics.CurrentEditorRenderMs;
        var combinedAverageMs = metrics.AverageHostRenderMs + metrics.AverageEditorRenderMs;

        HostStatus = metrics.IsHostReady ? "Running" : "Waiting for viewport";
        CurrentFrameInterval = FormatMilliseconds(metrics.CurrentFrameIntervalMs, hasSamples);
        AverageFrameInterval = FormatMilliseconds(metrics.AverageFrameIntervalMs, hasSamples);
        MaximumFrameInterval = FormatMilliseconds(metrics.MaximumFrameIntervalMs, hasSamples);
        FrameRate = hasSamples && metrics.AverageFrameIntervalMs > 0.001
            ? $"{1000.0 / metrics.AverageFrameIntervalMs:F1} FPS"
            : "—";
        UnaccountedAverage = FormatMilliseconds(
            Math.Max(0.0, metrics.AverageFrameIntervalMs - combinedAverageMs), hasSamples);
        CombinedCurrent = FormatMilliseconds(combinedCurrentMs, hasSamples);
        CombinedAverage = FormatMilliseconds(combinedAverageMs, hasSamples);
        SampleCount = metrics.SampleCount.ToString("N0", CultureInfo.CurrentCulture);

        HostCurrent = FormatMilliseconds(metrics.CurrentHostRenderMs, hasSamples);
        HostAverage = FormatMilliseconds(metrics.AverageHostRenderMs, hasSamples);
        HostMaximum = FormatMilliseconds(metrics.MaximumHostRenderMs, hasSamples);
        HostShare = FormatShare(metrics.AverageHostRenderMs, combinedAverageMs, hasSamples);
        HostFrameCount = metrics.FrameCount.ToString("N0", CultureInfo.CurrentCulture);
        ViewportSize = metrics.Width > 0 && metrics.Height > 0 ? $"{metrics.Width:N0} × {metrics.Height:N0}" : "—";
        TargetRefreshRate = metrics.TargetRefreshHz > 0.0f ? $"{metrics.TargetRefreshHz:F1} Hz" : "—";
        GpuFrameTime = metrics.GpuFrameMs >= 0.0f ? $"{metrics.GpuFrameMs:F2} ms" : "—";
        LastResize = FormatMilliseconds(metrics.LastResizeMs, metrics.Width > 0);

        EditorCurrent = FormatMilliseconds(metrics.CurrentEditorRenderMs, hasSamples);
        EditorAverage = FormatMilliseconds(metrics.AverageEditorRenderMs, hasSamples);
        EditorMaximum = FormatMilliseconds(metrics.MaximumEditorRenderMs, hasSamples);
        EditorShare = FormatShare(metrics.AverageEditorRenderMs, combinedAverageMs, hasSamples);

        RefreshProcessMetrics();
    }

    private void RefreshProcessMetrics()
    {
        try
        {
            _process.Refresh();
            var now = Stopwatch.GetTimestamp();
            var cpuTime = _process.TotalProcessorTime;
            var elapsedSeconds = (now - _previousSampleTimestamp) / (double)Stopwatch.Frequency;
            var cpuSeconds = (cpuTime - _previousCpuTime).TotalSeconds;
            var cpuPercent = elapsedSeconds > 0.0
                ? Math.Clamp(cpuSeconds / elapsedSeconds / Math.Max(Environment.ProcessorCount, 1) * 100.0, 0.0, 100.0)
                : 0.0;
            _previousCpuTime = cpuTime;
            _previousSampleTimestamp = now;

            ProcessCpu = $"{cpuPercent:F1}%";
            WorkingSet = FormatBytes(_process.WorkingSet64);
            PrivateMemory = FormatBytes(_process.PrivateMemorySize64);
            ManagedMemory = FormatBytes(GC.GetTotalMemory(false));
            ThreadCount = _process.Threads.Count.ToString("N0", CultureInfo.CurrentCulture);
            GarbageCollections = $"Gen 0: {GC.CollectionCount(0):N0}  ·  Gen 1: {GC.CollectionCount(1):N0}  ·  Gen 2: {GC.CollectionCount(2):N0}";
            Uptime = FormatDuration(DateTime.Now - _process.StartTime);
        }
        catch (InvalidOperationException)
        {
            ProcessCpu = WorkingSet = PrivateMemory = ManagedMemory = ThreadCount = GarbageCollections = Uptime = "—";
        }
    }

    internal string BuildMetricsReport()
    {
        Refresh();
        var report = new StringBuilder();
        report.AppendLine("PlutoGE Performance Metrics");
        report.AppendLine($"Captured: {DateTimeOffset.Now:O}");
        report.AppendLine($"Scope: {SelectedScope}");

        if (ShowCombined)
        {
            report.AppendLine();
            report.AppendLine("[Both]");
            AppendMetric(report, "Render callback current", CombinedCurrent);
            AppendMetric(report, "Render callback average", CombinedAverage);
            AppendMetric(report, "Frame interval current", CurrentFrameInterval);
            AppendMetric(report, "Frame interval average", AverageFrameInterval);
            AppendMetric(report, "Frame interval maximum", MaximumFrameInterval);
            AppendMetric(report, "Frame rate", FrameRate);
            AppendMetric(report, "Outside callback average", UnaccountedAverage);
            AppendMetric(report, "Rolling samples", SampleCount);
        }

        if (ShowHost)
        {
            report.AppendLine();
            report.AppendLine("[Host]");
            AppendMetric(report, "Status", HostStatus);
            AppendMetric(report, "Render current", HostCurrent);
            AppendMetric(report, "Render average", HostAverage);
            AppendMetric(report, "Render maximum", HostMaximum);
            AppendMetric(report, "Callback share", HostShare);
            AppendMetric(report, "Rendered frames", HostFrameCount);
            AppendMetric(report, "Viewport", ViewportSize);
            AppendMetric(report, "Observed refresh", TargetRefreshRate);
            AppendMetric(report, "GPU passes", GpuFrameTime);
            AppendMetric(report, "Last resize", LastResize);
        }

        if (ShowEditor)
        {
            report.AppendLine();
            report.AppendLine("[Editor]");
            AppendMetric(report, "Render overhead current", EditorCurrent);
            AppendMetric(report, "Render overhead average", EditorAverage);
            AppendMetric(report, "Render overhead maximum", EditorMaximum);
            AppendMetric(report, "Callback share", EditorShare);
            AppendMetric(report, "Process CPU", ProcessCpu);
            AppendMetric(report, "Working set", WorkingSet);
            AppendMetric(report, "Private memory", PrivateMemory);
            AppendMetric(report, "Managed memory", ManagedMemory);
            AppendMetric(report, "Threads", ThreadCount);
            AppendMetric(report, "GC collections", GarbageCollections);
            AppendMetric(report, "Uptime", Uptime);
        }

        return report.ToString();
    }

    internal void SetCopyStatus(string status) => CopyStatus = status;

    private static void AppendMetric(StringBuilder report, string name, string value) =>
        report.Append(name).Append(": ").AppendLine(value);

    private static string FormatMilliseconds(double value, bool available) => available ? $"{value:F2} ms" : "—";

    private static string FormatShare(double value, double total, bool available) =>
        available && total > 0.0001 ? $"{value / total * 100.0:F1}%" : "—";

    private static string FormatBytes(long bytes)
    {
        const double kibibyte = 1024.0;
        const double mebibyte = kibibyte * 1024.0;
        const double gibibyte = mebibyte * 1024.0;
        return bytes >= gibibyte ? $"{bytes / gibibyte:F2} GiB" : $"{bytes / mebibyte:F1} MiB";
    }

    private static string FormatDuration(TimeSpan duration) => duration.TotalHours >= 1.0
        ? $"{(int)duration.TotalHours}h {duration.Minutes}m {duration.Seconds}s"
        : $"{duration.Minutes}m {duration.Seconds}s";

    public void Dispose()
    {
        _timer.Stop();
        _timer.Tick -= OnTick;
        _process.Dispose();
    }
}
