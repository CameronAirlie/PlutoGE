using PlutoGE.Editor.Avalonia.Native;
using System.Collections.ObjectModel;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class TransformInspectorViewModel : ObservableObject
{
    private readonly EngineHost _host;
    private uint? _entityId;
    private bool _suspendWrites;
    private DateTime _lastWrite = DateTime.MinValue;
    private DateTime _lastComponentRefresh = DateTime.MinValue;
    private string _entityName = "Nothing selected";
    private bool _hasSelection;
    private bool _hasTransform;
    private decimal _positionX;
    private decimal _positionY;
    private decimal _positionZ;
    private decimal _rotationX;
    private decimal _rotationY;
    private decimal _rotationZ;
    private decimal _scaleX = 1;
    private decimal _scaleY = 1;
    private decimal _scaleZ = 1;

    public TransformInspectorViewModel(EngineHost host)
    {
        _host = host;
    }

    public string EntityName
    {
        get => _entityName;
        private set => SetProperty(ref _entityName, value);
    }

    public bool HasSelection
    {
        get => _hasSelection;
        private set => SetProperty(ref _hasSelection, value);
    }

    public bool HasTransform
    {
        get => _hasTransform;
        private set => SetProperty(ref _hasTransform, value);
    }

    public ObservableCollection<ComponentViewModel> Components { get; } = [];

    public string ComponentCountText => Components.Count == 1 ? "1 component" : $"{Components.Count} components";

    public decimal PositionX { get => _positionX; set => SetComponent(ref _positionX, value); }
    public decimal PositionY { get => _positionY; set => SetComponent(ref _positionY, value); }
    public decimal PositionZ { get => _positionZ; set => SetComponent(ref _positionZ, value); }
    public decimal RotationX { get => _rotationX; set => SetComponent(ref _rotationX, value); }
    public decimal RotationY { get => _rotationY; set => SetComponent(ref _rotationY, value); }
    public decimal RotationZ { get => _rotationZ; set => SetComponent(ref _rotationZ, value); }
    public decimal ScaleX { get => _scaleX; set => SetComponent(ref _scaleX, value); }
    public decimal ScaleY { get => _scaleY; set => SetComponent(ref _scaleY, value); }
    public decimal ScaleZ { get => _scaleZ; set => SetComponent(ref _scaleZ, value); }

    public void Select(EntityNodeViewModel? entity)
    {
        _entityId = entity?.Id;
        EntityName = entity?.Name ?? "Nothing selected";
        HasSelection = entity is not null;
        HasTransform = false;
        ApplyTransform(new EntityTransform(0, 0, 0, 0, 0, 0, 1, 1, 1));
        Components.Clear();
        OnPropertyChanged(nameof(ComponentCountText));
        RefreshFromNative(force: true);
    }

    public void RefreshFromNative(bool force = false)
    {
        if (_entityId is not { } entityId)
        {
            return;
        }

        // Do not replace a value while a NumericUpDown is committing rapid edits.
        if (!force && DateTime.UtcNow - _lastWrite < TimeSpan.FromMilliseconds(600))
        {
            return;
        }

        if (_host.ReadTransform(entityId) is not { } transform)
        {
            return;
        }

        ApplyTransform(transform);
        HasTransform = true;

        if (force || DateTime.UtcNow - _lastComponentRefresh >= TimeSpan.FromSeconds(1))
        {
            _lastComponentRefresh = DateTime.UtcNow;
            Components.Clear();
            foreach (var component in _host.ReadComponents(entityId))
            {
                Components.Add(new ComponentViewModel(
                    component.Name,
                    component.Enabled,
                    component.Properties.Select(property => new ComponentPropertyViewModel(property.Name, property.Value)).ToArray()));
            }
            OnPropertyChanged(nameof(ComponentCountText));
        }
    }

    private void SetComponent(ref decimal field, decimal value)
    {
        if (!SetProperty(ref field, value) || _suspendWrites || !HasTransform || _entityId is not { } entityId)
        {
            return;
        }

        _lastWrite = DateTime.UtcNow;
        try
        {
            var committed = _host.WriteTransform(entityId, new EntityTransform(
                (float)PositionX, (float)PositionY, (float)PositionZ,
                (float)RotationX, (float)RotationY, (float)RotationZ,
                (float)ScaleX, (float)ScaleY, (float)ScaleZ));
            ApplyTransform(committed);
        }
        catch (Exception exception)
        {
            _host.ReportStatus(exception.Message);
            RefreshFromNative(force: true);
        }
    }

    private void ApplyTransform(EntityTransform transform)
    {
        _suspendWrites = true;
        try
        {
            PositionX = (decimal)transform.PositionX;
            PositionY = (decimal)transform.PositionY;
            PositionZ = (decimal)transform.PositionZ;
            RotationX = (decimal)transform.RotationX;
            RotationY = (decimal)transform.RotationY;
            RotationZ = (decimal)transform.RotationZ;
            ScaleX = (decimal)transform.ScaleX;
            ScaleY = (decimal)transform.ScaleY;
            ScaleZ = (decimal)transform.ScaleZ;
        }
        finally
        {
            _suspendWrites = false;
        }
    }
}
