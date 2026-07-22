namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed partial class SceneCameraSettingsViewModel : ObservableObject
{
    private decimal _fieldOfView = 45m;
    private decimal _nearPlane = 0.1m;
    private decimal _farPlane = 1000m;
    private decimal _moveSpeed = 6m;
    private decimal _boostSpeed = 18m;
    private decimal _lookSensitivity = 0.18m;

    internal SceneCameraSettingsViewModel() { }

    public decimal FieldOfView
    {
        get => _fieldOfView;
        set => SetProperty(ref _fieldOfView, Math.Clamp(value, 10m, 150m));
    }

    public decimal NearPlane
    {
        get => _nearPlane;
        set => SetProperty(ref _nearPlane, Math.Clamp(value, 0.001m, Math.Max(0.001m, FarPlane - 0.001m)));
    }

    public decimal FarPlane
    {
        get => _farPlane;
        set => SetProperty(ref _farPlane, Math.Clamp(value, NearPlane + 0.001m, 100000m));
    }

    public decimal MoveSpeed
    {
        get => _moveSpeed;
        set => SetProperty(ref _moveSpeed, Math.Clamp(value, 0.01m, 10000m));
    }

    public decimal BoostSpeed
    {
        get => _boostSpeed;
        set => SetProperty(ref _boostSpeed, Math.Clamp(value, 0.01m, 50000m));
    }

    public decimal LookSensitivity
    {
        get => _lookSensitivity;
        set => SetProperty(ref _lookSensitivity, Math.Clamp(value, 0.01m, 2m));
    }
}
