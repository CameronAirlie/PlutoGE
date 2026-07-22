using System.Diagnostics;
using System.Numerics;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.OpenGL;
using Avalonia.OpenGL.Controls;
using Avalonia.VisualTree;
using PlutoGE.Editor.Avalonia.Native;
using PlutoGE.Editor.Avalonia.ViewModels;

namespace PlutoGE.Editor.Avalonia.Controls;

internal sealed class EngineViewport : OpenGlControlBase
{
    internal static readonly StyledProperty<ViewportViewModel?> ViewModelProperty =
        AvaloniaProperty.Register<EngineViewport, ViewportViewModel?>(nameof(ViewModel));

    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly HashSet<Key> _keys = [];
    private readonly object _inputSync = new();
    private TopLevel? _inputRoot;
    private PlutoNative.GlProcAddress? _resolver;
    private nint _resolverPointer;
    private ulong _viewport;
    private long _previousTicks;
    private Vector3 _cameraPosition = new(0.0f, 2.0f, 6.0f);
    private Vector3 _cameraPivot;
    private float _cameraOrbitDistance = 6.0f;
    private float _cameraYaw;
    private float _cameraPitch = -10.0f;
    private float _mouseX;
    private float _mouseY;
    private float _wheel;
    private bool _left;
    private bool _right;
    private bool _middle;
    private bool _pointerInside;
    private bool _inputFocused;
    private bool _hasLastPointer;
    private float _lastPointerX;
    private float _lastPointerY;
    private float _smoothedRefreshHz = 60.0f;
    private string? _lastError;

    internal ViewportViewModel? ViewModel
    {
        get => GetValue(ViewModelProperty);
        set => SetValue(ViewModelProperty, value);
    }

    private EngineHost? Host => ViewModel?.Host;

    public EngineViewport()
    {
        Focusable = true;
        IsTabStop = true;
        ClipToBounds = true;
        _cameraPivot = _cameraPosition +
            ViewportCameraController.Forward(_cameraYaw, _cameraPitch) * _cameraOrbitDistance;

        AddHandler(KeyDownEvent, OnKeyDown,
            RoutingStrategies.Tunnel, handledEventsToo: true);
        AddHandler(KeyUpEvent, OnKeyUp,
            RoutingStrategies.Tunnel, handledEventsToo: true);

        PointerEntered += (_, _) =>
        {
            lock (_inputSync) _pointerInside = true;
        };
        PointerExited += (_, _) =>
        {
            lock (_inputSync) _pointerInside = false;
        };
        PointerCaptureLost += (_, _) =>
        {
            lock (_inputSync)
            {
                _left = _right = _middle = false;
                _hasLastPointer = false;
            }
        };
        GotFocus += (_, _) =>
        {
            lock (_inputSync) _inputFocused = true;
        };
        LostFocus += (_, _) =>
        {
            lock (_inputSync)
            {
                _inputFocused = false;
                _keys.Clear();
            }
        };
    }

    protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
    {
        base.OnAttachedToVisualTree(e);
        AttachInputRoot(TopLevel.GetTopLevel(this));
    }

    protected override void OnDetachedFromVisualTree(VisualTreeAttachmentEventArgs e)
    {
        AttachInputRoot(null);
        base.OnDetachedFromVisualTree(e);
    }

    private void AttachInputRoot(TopLevel? inputRoot)
    {
        if (ReferenceEquals(_inputRoot, inputRoot)) return;
        if (_inputRoot is not null)
        {
            _inputRoot.RemoveHandler(PointerMovedEvent, OnPointerMoved);
            _inputRoot.RemoveHandler(PointerPressedEvent, OnPointerPressed);
            _inputRoot.RemoveHandler(PointerReleasedEvent, OnPointerReleased);
            _inputRoot.RemoveHandler(PointerWheelChangedEvent, OnPointerWheelChanged);
        }

        _inputRoot = inputRoot;
        if (_inputRoot is null) return;
        // Observe at the window root. If OpenGlControlBase's composition visual
        // wins hit testing, the viewport is not in the routed-event path at all,
        // so a handled-events handler installed on the viewport cannot run.
        _inputRoot.AddHandler(PointerMovedEvent, OnPointerMoved,
            RoutingStrategies.Tunnel, handledEventsToo: true);
        _inputRoot.AddHandler(PointerPressedEvent, OnPointerPressed,
            RoutingStrategies.Tunnel, handledEventsToo: true);
        _inputRoot.AddHandler(PointerReleasedEvent, OnPointerReleased,
            RoutingStrategies.Tunnel, handledEventsToo: true);
        _inputRoot.AddHandler(PointerWheelChangedEvent, OnPointerWheelChanged,
            RoutingStrategies.Tunnel, handledEventsToo: true);
    }

    protected override void OnOpenGlInit(GlInterface gl)
    {
        try
        {
            _resolver = (name, _) =>
            {
                var symbol = Marshal.PtrToStringUTF8(name);
                return symbol is null ? 0 : gl.GetProcAddress(symbol);
            };
            _resolverPointer = Marshal.GetFunctionPointerForDelegate(_resolver);
            var scaling = (float)(TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0);
            _viewport = Host?.AcquireViewport(_resolver,
                Math.Max(1, (int)(Bounds.Width * scaling)),
                Math.Max(1, (int)(Bounds.Height * scaling))) ?? 0;
            ViewModel?.Attach(_viewport);
            _previousTicks = _clock.ElapsedTicks;
            _lastError = null;
        }
        catch (Exception exception)
        {
            _lastError = exception.Message;
            Host?.ReportStatus(exception.Message);
        }
    }

    protected override void OnOpenGlDeinit(GlInterface gl)
    {
        if (_viewport != 0)
        {
            Host?.ReleaseViewport(_viewport);
            ViewModel?.Detach(_viewport);
            _viewport = 0;
        }
        _resolver = null;
        _resolverPointer = 0;
    }

    protected override void OnOpenGlLost()
    {
        ViewModel?.Detach(_viewport);
        _viewport = 0;
        Host?.ReportStatus("OpenGL context lost; the viewport will be recreated by Avalonia.");
    }

    protected override void OnOpenGlRender(GlInterface gl, int fb)
    {
        if (_viewport == 0 || Host is null || _resolverPointer == 0)
        {
            return;
        }

        var now = _clock.ElapsedTicks;
        var delta = _previousTicks == 0 ? 1.0f / 60.0f : (float)((now - _previousTicks) / (double)Stopwatch.Frequency);
        _previousTicks = now;
        delta = Math.Clamp(delta, 1.0f / 1000.0f, 0.1f);
        var instantaneousHz = 1.0f / delta;
        _smoothedRefreshHz += (instantaneousHz - _smoothedRefreshHz) * 0.05f;
        float mouseX;
        float mouseY;
        float mouseWheel;
        bool mouseLeft;
        bool mouseRight;
        bool mouseMiddle;
        bool inputFocused;
        Vector3 cameraPosition;
        float cameraYaw;
        float cameraPitch;

        lock (_inputSync)
        {
            _cameraYaw = ViewportValidationMath.AdvanceYaw(
                _cameraYaw, ViewModel?.ContinuousYawDegreesPerSecond ?? 0.0f, delta);
            UpdateCamera(delta);

            var pointerAvailable = _pointerInside || _left || _right || _middle;
            mouseX = pointerAvailable ? _mouseX : float.MinValue;
            mouseY = pointerAvailable ? _mouseY : float.MinValue;
            mouseWheel = _wheel;
            mouseLeft = _left;
            mouseRight = _right;
            mouseMiddle = _middle;
            inputFocused = _inputFocused || pointerAvailable;
            cameraPosition = _cameraPosition;
            cameraYaw = _cameraYaw;
            cameraPitch = _cameraPitch;
            _wheel = 0.0f;
        }

        var scaling = (float)(TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0);
        var frame = new PlutoNative.ViewportFrame
        {
            StructSize = (uint)Marshal.SizeOf<PlutoNative.ViewportFrame>(),
            Width = Math.Max(1, (int)(Bounds.Width * scaling)),
            Height = Math.Max(1, (int)(Bounds.Height * scaling)),
            Framebuffer = fb,
            DeltaSeconds = delta,
            TargetRefreshHz = _smoothedRefreshHz,
            MouseX = mouseX == float.MinValue ? -float.MaxValue : mouseX * scaling,
            MouseY = mouseY == float.MinValue ? -float.MaxValue : mouseY * scaling,
            MouseWheel = mouseWheel,
            MouseLeft = mouseLeft ? (byte)1 : (byte)0,
            MouseRight = mouseRight ? (byte)1 : (byte)0,
            MouseMiddle = mouseMiddle ? (byte)1 : (byte)0,
            Focused = inputFocused ? (byte)1 : (byte)0,
            CameraX = cameraPosition.X,
            CameraY = cameraPosition.Y,
            CameraZ = cameraPosition.Z,
            CameraYawDegrees = cameraYaw,
            CameraPitchDegrees = cameraPitch,
            CameraFovDegrees = (float)(ViewModel?.Settings.FieldOfView ?? 45m),
            CameraNearPlane = (float)(ViewModel?.Settings.NearPlane ?? 0.1m),
            CameraFarPlane = (float)(ViewModel?.Settings.FarPlane ?? 1000m),
            GetProcAddress = _resolverPointer,
        };
        var result = Host.Render(_viewport, ViewModel?.SelectedEntityId ?? 0, in frame, out var gizmoActive);
        if (result != PlutoNative.Result.Ok)
        {
            var error = PlutoNative.GetLastError();
            if (!string.Equals(error, _lastError, StringComparison.Ordinal))
            {
                _lastError = error;
                Host.ReportStatus(error);
            }
        }
        else
        {
            _lastError = null;
            if (gizmoActive) ViewModel?.NotifyTransformManipulated();
        }

        RequestNextFrameRendering();
    }

    private void OnPointerMoved(object? sender, PointerEventArgs args)
    {
        var position = args.GetPosition(this);
        var point = args.GetCurrentPoint(this);
        var inside = Contains(position);
        lock (_inputSync)
        {
            _pointerInside = inside;
            if (!inside && !_left && !_right && !_middle) return;
            _mouseX = (float)position.X;
            _mouseY = (float)position.Y;
            _left = point.Properties.IsLeftButtonPressed;
            _right = point.Properties.IsRightButtonPressed;
            _middle = point.Properties.IsMiddleButtonPressed;
            if (_hasLastPointer)
            {
                var deltaX = _mouseX - _lastPointerX;
                var deltaY = _mouseY - _lastPointerY;
                var alt = args.KeyModifiers.HasFlag(KeyModifiers.Alt);
                if (_left && alt)
                {
                    (_cameraYaw, _cameraPitch) = ViewportCameraController.AdvanceLook(
                        _cameraYaw, _cameraPitch, deltaX, deltaY, (float)(ViewModel?.Settings.LookSensitivity ?? 0.18m));
                    _cameraPosition = ViewportCameraController.OrbitPosition(
                        _cameraPivot, _cameraYaw, _cameraPitch, _cameraOrbitDistance);
                }
                else if (_middle)
                {
                    var unitsPerPixel = Math.Max(_cameraOrbitDistance, 0.25f) /
                                        Math.Max((float)Bounds.Height, 1.0f) * 1.7f;
                    (_cameraPosition, _cameraPivot) = ViewportCameraController.Pan(
                        _cameraPosition, _cameraPivot, _cameraYaw, _cameraPitch,
                        deltaX, deltaY, unitsPerPixel);
                }
                else if (_right && alt)
                {
                    _cameraOrbitDistance = ViewportCameraController.DollyDistance(
                        _cameraOrbitDistance, -deltaY * 0.08f);
                    _cameraPosition = ViewportCameraController.OrbitPosition(
                        _cameraPivot, _cameraYaw, _cameraPitch, _cameraOrbitDistance);
                }
                else if (_right)
                {
                    (_cameraYaw, _cameraPitch) = ViewportCameraController.AdvanceLook(
                        _cameraYaw, _cameraPitch, deltaX, deltaY, (float)(ViewModel?.Settings.LookSensitivity ?? 0.18m));
                    _cameraPivot = _cameraPosition +
                        ViewportCameraController.Forward(_cameraYaw, _cameraPitch) * _cameraOrbitDistance;
                }
            }
            _lastPointerX = _mouseX;
            _lastPointerY = _mouseY;
            _hasLastPointer = true;
        }
        RequestNextFrameRendering();
    }

    private void OnPointerPressed(object? sender, PointerPressedEventArgs args)
    {
        var position = args.GetPosition(this);
        if (!Contains(position)) return;
        Focus(NavigationMethod.Pointer);
        var point = args.GetCurrentPoint(this);
        lock (_inputSync)
        {
            _inputFocused = true;
            _pointerInside = true;
            _mouseX = (float)position.X;
            _mouseY = (float)position.Y;
            _left = point.Properties.IsLeftButtonPressed;
            _right = point.Properties.IsRightButtonPressed;
            _middle = point.Properties.IsMiddleButtonPressed;
            _lastPointerX = _mouseX;
            _lastPointerY = _mouseY;
            _hasLastPointer = true;
        }
        args.Pointer.Capture(this);
        args.Handled = true;
        if (point.Properties.PointerUpdateKind == PointerUpdateKind.LeftButtonPressed &&
            !args.KeyModifiers.HasFlag(KeyModifiers.Alt) && ViewModel is not null)
        {
            var scaling = (float)(TopLevel.GetTopLevel(this)?.RenderScaling ?? 1.0);
            ViewModel.PickEntity((float)position.X * scaling, (float)position.Y * scaling);
        }
        RequestNextFrameRendering();
    }

    private void OnPointerReleased(object? sender, PointerReleasedEventArgs args)
    {
        lock (_inputSync)
            if (!_left && !_right && !_middle) return;

        var point = args.GetCurrentPoint(this);
        var releaseCapture = false;
        lock (_inputSync)
        {
            _left = point.Properties.IsLeftButtonPressed;
            _right = point.Properties.IsRightButtonPressed;
            _middle = point.Properties.IsMiddleButtonPressed;
            releaseCapture = !_left && !_right && !_middle;
        }
        if (releaseCapture) args.Pointer.Capture(null);
        args.Handled = true;
        RequestNextFrameRendering();
    }

    private void OnPointerWheelChanged(object? sender, PointerWheelEventArgs args)
    {
        if (!Contains(args.GetPosition(this))) return;
        lock (_inputSync)
        {
            var wheelDelta = (float)args.Delta.Y;
            _wheel += wheelDelta;
            _cameraOrbitDistance = ViewportCameraController.DollyDistance(_cameraOrbitDistance, wheelDelta);
            _cameraPosition = ViewportCameraController.OrbitPosition(
                _cameraPivot, _cameraYaw, _cameraPitch, _cameraOrbitDistance);
        }
        args.Handled = true;
        RequestNextFrameRendering();
    }

    private bool Contains(Point position) =>
        position.X >= 0.0 && position.Y >= 0.0 &&
        position.X < Bounds.Width && position.Y < Bounds.Height;

    private void OnKeyDown(object? sender, KeyEventArgs args)
    {
        bool cameraLookActive;
        lock (_inputSync)
        {
            _keys.Add(args.Key);
            cameraLookActive = _right;
        }
        if (_viewport == 0 || ViewModel is null) return;
        if (args.Key == Key.F && ViewModel.SelectedEntityId != 0 &&
            Host?.ReadTransform(ViewModel.SelectedEntityId) is { } transform)
        {
            var maximumScale = Math.Max(Math.Abs(transform.ScaleX),
                Math.Max(Math.Abs(transform.ScaleY), Math.Abs(transform.ScaleZ)));
            lock (_inputSync)
            {
                _cameraPivot = new Vector3(transform.PositionX, transform.PositionY, transform.PositionZ);
                _cameraOrbitDistance = ViewportCameraController.FrameDistance(maximumScale);
                _cameraPosition = ViewportCameraController.OrbitPosition(
                    _cameraPivot, _cameraYaw, _cameraPitch, _cameraOrbitDistance);
            }
            args.Handled = true;
            RequestNextFrameRendering();
            return;
        }
        if (args.Key == Key.W && !cameraLookActive) ViewModel.SetGizmoOperation(0);
        if (args.Key == Key.E && !cameraLookActive) ViewModel.SetGizmoOperation(1);
        if (args.Key == Key.R && !cameraLookActive) ViewModel.SetGizmoOperation(2);
        if (cameraLookActive || args.Key is Key.W or Key.A or Key.S or Key.D or Key.Q or Key.E)
        {
            args.Handled = true;
        }
    }

    private void OnKeyUp(object? sender, KeyEventArgs args)
    {
        lock (_inputSync) _keys.Remove(args.Key);
        if (args.Key is Key.W or Key.A or Key.S or Key.D or Key.Q or Key.E)
        {
            args.Handled = true;
        }
    }

    private void UpdateCamera(float deltaSeconds)
    {
        if (!_right) return;
        var localMovement = Vector3.Zero;
        if (_keys.Contains(Key.W)) localMovement.Z += 1.0f;
        if (_keys.Contains(Key.S)) localMovement.Z -= 1.0f;
        if (_keys.Contains(Key.D)) localMovement.X += 1.0f;
        if (_keys.Contains(Key.A)) localMovement.X -= 1.0f;
        if (_keys.Contains(Key.E)) localMovement.Y += 1.0f;
        if (_keys.Contains(Key.Q)) localMovement.Y -= 1.0f;
        var speed = _keys.Contains(Key.LeftShift) || _keys.Contains(Key.RightShift)
            ? (float)(ViewModel?.Settings.BoostSpeed ?? 18m)
            : (float)(ViewModel?.Settings.MoveSpeed ?? 6m);
        var previousPosition = _cameraPosition;
        _cameraPosition = ViewportCameraController.AdvancePosition(
            _cameraPosition, _cameraYaw, _cameraPitch, localMovement, speed, deltaSeconds);
        _cameraPivot += _cameraPosition - previousPosition;
    }
}
