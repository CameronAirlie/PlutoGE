using PlutoGE.Editor.Avalonia.Native;
using System.Collections.ObjectModel;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class TransformInspectorViewModel : ObservableObject
{
    private readonly EngineHost _host;
    private uint? _entityId;
    private bool _suspendWrites;
    private DateTime _lastWrite = DateTime.MinValue;
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
    public ObservableCollection<AddableComponentTypeViewModel> AddableComponentTypes { get; } = [];

    public string ComponentCountText => Components.Count == 1 ? "1 component" : $"{Components.Count} components";
    public bool HasComponents => Components.Count > 0;
    public bool HasNoComponents => Components.Count == 0;

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
        AddableComponentTypes.Clear();
        OnPropertyChanged(nameof(ComponentCountText));
        OnPropertyChanged(nameof(HasComponents));
        OnPropertyChanged(nameof(HasNoComponents));
        RefreshFromNative(force: true, refreshComponents: true);
    }

    public void RefreshFromNative(bool force = false, bool refreshComponents = false)
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

        if (refreshComponents)
        {
            IReadOnlyList<EntityComponent> nativeComponents;
            IReadOnlyList<AddableComponentTypeValue> addableComponentTypes;
            try
            {
                nativeComponents = _host.ReadComponents(entityId);
                addableComponentTypes = _host.ReadAddableComponentTypes(entityId);
            }
            catch (Exception exception)
            {
                _host.ReportStatus(exception.Message);
                return;
            }
            Components.Clear();
            foreach (var component in nativeComponents)
                Components.Add(new ComponentViewModel(_host, entityId, component, () => RemoveComponent(component.Index)));
            AddableComponentTypes.Clear();
            foreach (var componentType in addableComponentTypes)
                AddableComponentTypes.Add(new AddableComponentTypeViewModel(
                    componentType.TypeName, componentType.DisplayName, componentType.Category,
                    () => AddComponent(componentType.TypeName)));
            OnPropertyChanged(nameof(ComponentCountText));
            OnPropertyChanged(nameof(HasComponents));
            OnPropertyChanged(nameof(HasNoComponents));
        }
    }

    private void AddComponent(string typeName)
    {
        if (_entityId is not { } entityId) return;
        try
        {
            _host.AddComponent(entityId, typeName);
            RefreshFromNative(force: true, refreshComponents: true);
        }
        catch (Exception exception)
        {
            _host.ReportStatus(exception.Message);
        }
    }

    private void RemoveComponent(uint componentIndex)
    {
        if (_entityId is not { } entityId) return;
        try
        {
            _host.RemoveComponent(entityId, componentIndex);
            RefreshFromNative(force: true, refreshComponents: true);
        }
        catch (Exception exception)
        {
            _host.ReportStatus(exception.Message);
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

internal sealed class AddableComponentTypeViewModel
{
    internal AddableComponentTypeViewModel(string typeName, string displayName, string category, Action add)
    {
        TypeName = typeName;
        DisplayName = displayName;
        Category = category;
        AddCommand = new RelayCommand(add);
    }

    public string TypeName { get; }
    public string DisplayName { get; }
    public string Category { get; }
    public RelayCommand AddCommand { get; }
}
