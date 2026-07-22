using System.Globalization;
using PlutoGE.Editor.Avalonia.Native;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed record AssetItemViewModel(string Reference, string Type, bool IsScene, string SizeText)
{
    public string DisplayName
    {
        get
        {
            const string prefix = "project://";
            return Reference.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
                ? Reference[prefix.Length..]
                : Reference;
        }
    }
}

internal sealed class ComponentPropertyViewModel : ObservableObject
{
    private readonly EngineHost _host;
    private readonly uint _entityId;
    private readonly uint _componentIndex;
    private readonly uint _propertyIndex;
    private string _value;
    private decimal _numericValue;
    private bool _boolValue;
    private string? _selectedOption;

    internal ComponentPropertyViewModel(EngineHost host, uint entityId, uint componentIndex, ComponentPropertyValue property)
    {
        _host = host;
        _entityId = entityId;
        _componentIndex = componentIndex;
        _propertyIndex = property.Index;
        Name = property.Name;
        Type = property.Type;
        Editable = property.Editable;
        EnumOptions = property.EnumOptions;
        _value = property.Value;
        _boolValue = string.Equals(property.Value, "true", StringComparison.OrdinalIgnoreCase) || property.Value == "1";
        decimal.TryParse(property.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out _numericValue);
        _selectedOption = ResolveEnumOption(property.Value, property.EnumOptions);
    }

    public string Name { get; }
    public int Type { get; }
    public bool Editable { get; }
    public IReadOnlyList<string> EnumOptions { get; }
    public bool IsNumeric => Type is 0 or 1 or 8 or 9;
    public bool IsInteger => Type is 1 or 9;
    public decimal NumericIncrement => IsInteger ? 1m : 0.1m;
    public bool IsBoolean => Type == 4;
    public bool IsEnum => Type == 6;
    public bool IsText => !IsNumeric && !IsBoolean && !IsEnum;
    public bool IsReadOnly => !Editable;

    public string Value
    {
        get => _value;
        set
        {
            var previous = _value;
            if (!Editable || !SetProperty(ref _value, value ?? string.Empty)) return;
            if (!Commit(_value))
            {
                _value = previous;
                OnPropertyChanged();
            }
        }
    }

    public decimal NumericValue
    {
        get => _numericValue;
        set
        {
            var previous = _numericValue;
            if (!Editable || !SetProperty(ref _numericValue, value)) return;
            var serialized = IsInteger
                ? decimal.Truncate(value).ToString(CultureInfo.InvariantCulture)
                : value.ToString(CultureInfo.InvariantCulture);
            _value = serialized;
            if (!Commit(serialized))
            {
                _numericValue = previous;
                OnPropertyChanged();
            }
        }
    }

    public bool BoolValue
    {
        get => _boolValue;
        set
        {
            var previous = _boolValue;
            if (!Editable || !SetProperty(ref _boolValue, value)) return;
            _value = value ? "true" : "false";
            if (!Commit(_value))
            {
                _boolValue = previous;
                OnPropertyChanged();
            }
        }
    }

    public string? SelectedOption
    {
        get => _selectedOption;
        set
        {
            var previous = _selectedOption;
            if (!Editable || value is null || !SetProperty(ref _selectedOption, value)) return;
            var serialized = GetEnumOptionIndex(value, EnumOptions).ToString(CultureInfo.InvariantCulture);
            _value = serialized;
            if (!Commit(serialized))
            {
                _selectedOption = previous;
                OnPropertyChanged();
            }
        }
    }

    private bool Commit(string value)
    {
        try
        {
            _host.WriteComponentProperty(_entityId, _componentIndex, _propertyIndex, value);
            return true;
        }
        catch (Exception exception)
        {
            _host.ReportStatus(exception.Message);
            return false;
        }
    }

    private static string? ResolveEnumOption(string value, IReadOnlyList<string> options)
    {
        if (int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var index) &&
            index >= 0 && index < options.Count)
            return options[index];
        return options.FirstOrDefault(option => string.Equals(option, value, StringComparison.Ordinal));
    }

    private static int GetEnumOptionIndex(string value, IReadOnlyList<string> options)
    {
        for (var index = 0; index < options.Count; ++index)
            if (string.Equals(options[index], value, StringComparison.Ordinal)) return index;
        return 0;
    }
}

internal sealed class ComponentViewModel : ObservableObject
{
    private readonly EngineHost _host;
    private readonly uint _entityId;
    private readonly uint _componentIndex;
    private bool _enabled;

    internal ComponentViewModel(EngineHost host, uint entityId, EntityComponent component)
    {
        _host = host;
        _entityId = entityId;
        _componentIndex = component.Index;
        Name = component.Name;
        _enabled = component.Enabled;
        Properties = component.Properties
            .Select(property => new ComponentPropertyViewModel(host, entityId, component.Index, property))
            .ToArray();
    }

    public string Name { get; }
    public IReadOnlyList<ComponentPropertyViewModel> Properties { get; }
    public bool Enabled
    {
        get => _enabled;
        set
        {
            var previous = _enabled;
            if (!SetProperty(ref _enabled, value)) return;
            try
            {
                _host.WriteComponentEnabled(_entityId, _componentIndex, value);
                OnPropertyChanged(nameof(StateText));
            }
            catch (Exception exception)
            {
                _enabled = previous;
                OnPropertyChanged();
                OnPropertyChanged(nameof(StateText));
                _host.ReportStatus(exception.Message);
            }
        }
    }

    public string StateText => Enabled ? "Enabled" : "Disabled";
    public bool HasProperties => Properties.Count > 0;
}
