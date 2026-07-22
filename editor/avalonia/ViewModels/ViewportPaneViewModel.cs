using System.Windows.Input;
using Avalonia.Threading;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class ViewportPaneViewModel : ObservableObject, IDisposable
{
    private readonly EditorSessionViewModel _session;
    private readonly DispatcherTimer _timer;
    private int _transformRefreshPending;

    internal ViewportPaneViewModel(EditorSessionViewModel session)
    {
        _session = session;
        Viewport = new ViewportViewModel(session.Host, session.SceneCamera);
        Viewport.SelectEntity(session.SelectedEntity?.Id ?? 0);
        Viewport.EntityPicked += OnEntityPicked;
        Viewport.TransformManipulated += OnTransformManipulated;
        session.SelectionChanged += OnSelectionChanged;
        MoveCommand = new RelayCommand(() => Viewport.SetGizmoOperation(0));
        RotateCommand = new RelayCommand(() => Viewport.SetGizmoOperation(1));
        ScaleCommand = new RelayCommand(() => Viewport.SetGizmoOperation(2));
        _timer = new DispatcherTimer(DispatcherPriority.Background)
        {
            Interval = TimeSpan.FromMilliseconds(500),
        };
        _timer.Tick += OnTick;
        _timer.Start();
    }

    public ViewportViewModel Viewport { get; }
    public EditorSessionViewModel Session => _session;
    public ICommand MoveCommand { get; }
    public ICommand RotateCommand { get; }
    public ICommand ScaleCommand { get; }
    public string ActiveScene => _session.ActiveScene;
    public string FrameStats => Viewport.FrameStats;

    private void OnTick(object? sender, EventArgs e)
    {
        Viewport.RefreshStats();
        OnPropertyChanged(nameof(FrameStats));
        OnPropertyChanged(nameof(ActiveScene));
    }

    private void OnEntityPicked(uint entityId) => _session.SelectEntity(entityId);
    private void OnSelectionChanged(uint entityId) => Viewport.SelectEntity(entityId);

    private void OnTransformManipulated()
    {
        if (Interlocked.Exchange(ref _transformRefreshPending, 1) != 0) return;
        Dispatcher.UIThread.Post(() =>
        {
            Interlocked.Exchange(ref _transformRefreshPending, 0);
            _session.Inspector.RefreshFromNative(force: true);
        }, DispatcherPriority.Background);
    }

    internal void EnableValidation() => Viewport.ContinuousYawDegreesPerSecond = 35.0f;

    public void Dispose()
    {
        _timer.Stop();
        _timer.Tick -= OnTick;
        Viewport.EntityPicked -= OnEntityPicked;
        Viewport.TransformManipulated -= OnTransformManipulated;
        _session.SelectionChanged -= OnSelectionChanged;
    }
}
