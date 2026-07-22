using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Input;
using Avalonia.Media;
using PlutoGE.Editor.Avalonia.Native;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed class AssetItemViewModel
{
    internal AssetItemViewModel(string reference, string type, bool isScene, string sizeText, string absolutePath = "")
    {
        Reference = reference;
        Type = type;
        IsScene = isScene;
        SizeText = sizeText;
        AbsolutePath = absolutePath;
        RelativePath = StripScheme(reference).Replace('\\', '/').Trim('/');
        DisplayName = Path.GetFileName(RelativePath);
        FolderPath = (Path.GetDirectoryName(RelativePath) ?? string.Empty).Replace('\\', '/');
    }

    private AssetItemViewModel(string folderPath)
    {
        IsFolder = true;
        Type = "Folder";
        RelativePath = folderPath.Replace('\\', '/').Trim('/');
        FolderPath = (Path.GetDirectoryName(RelativePath) ?? string.Empty).Replace('\\', '/');
        DisplayName = Path.GetFileName(RelativePath);
        Reference = RelativePath;
        SizeText = string.Empty;
        AbsolutePath = string.Empty;
    }

    public string Reference { get; }
    public string Type { get; }
    public bool IsScene { get; }
    public bool IsFolder { get; }
    public string SizeText { get; }
    public string AbsolutePath { get; }
    public string RelativePath { get; }
    public string FolderPath { get; }
    public string DisplayName { get; }
    public string IconGlyph => IsFolder ? "▰" : IsScene ? "◇" : Type switch
    {
        "Script" => "#",
        "Material" => "●",
        "Texture" => "▧",
        "Audio" => "♫",
        "Mesh" or "Model" => "◆",
        _ => "□",
    };

    internal static AssetItemViewModel Folder(string path) => new(path);

    private static string StripScheme(string reference)
    {
        const string prefix = "project://";
        return reference.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) ? reference[prefix.Length..] : reference;
    }
}

internal sealed class AssetFolderViewModel
{
    internal AssetFolderViewModel(string name, string path)
    {
        Name = name;
        Path = path;
    }

    public string Name { get; }
    public string Path { get; }
    public ObservableCollection<AssetFolderViewModel> Children { get; } = [];
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
    private Color _colorValue;

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
        _colorValue = ParseColor(property.Value);
    }

    public string Name { get; }
    public int Type { get; }
    public bool Editable { get; }
    public IReadOnlyList<string> EnumOptions { get; }
    public bool IsNumeric => Type is 0 or 1 or 8 or 9;
    public bool IsInteger => Type is 1 or 9;
    public decimal NumericIncrement => IsInteger ? 1m : 0.1m;
    public bool IsBoolean => Type == 4;
    public bool IsColor => Type == 5;
    public bool IsEnum => Type == 6;
    public bool IsText => !IsNumeric && !IsBoolean && !IsColor && !IsEnum;
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

    public Color ColorValue
    {
        get => _colorValue;
        set
        {
            var previous = _colorValue;
            if (!Editable || !SetProperty(ref _colorValue, value)) return;
            var serialized = SerializeColor(value);
            _value = serialized;
            if (!Commit(serialized))
            {
                _colorValue = previous;
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

    private static Color ParseColor(string value)
    {
        var channels = value.Split(',', StringSplitOptions.TrimEntries);
        var values = new[] { 1f, 1f, 1f, 1f };
        for (var index = 0; index < Math.Min(channels.Length, values.Length); ++index)
            if (float.TryParse(channels[index], NumberStyles.Float, CultureInfo.InvariantCulture, out var channel))
                values[index] = Math.Clamp(channel, 0f, 1f);
        static byte ToByte(float channel) => (byte)Math.Round(channel * 255f);
        return Color.FromArgb(ToByte(values[3]), ToByte(values[0]), ToByte(values[1]), ToByte(values[2]));
    }

    private static string SerializeColor(Color color)
    {
        static string Channel(byte value) => (value / 255f).ToString("0.######", CultureInfo.InvariantCulture);
        return $"{Channel(color.R)},{Channel(color.G)},{Channel(color.B)},{Channel(color.A)}";
    }
}

internal sealed class ComponentViewModel : ObservableObject
{
    private readonly EngineHost _host;
    private readonly uint _entityId;
    private readonly uint _componentIndex;
    private bool _enabled;

    internal ComponentViewModel(EngineHost host, uint entityId, EntityComponent component, Action remove)
    {
        _host = host;
        _entityId = entityId;
        _componentIndex = component.Index;
        Name = component.Name;
        _enabled = component.Enabled;
        Properties = component.Properties
            .Select(property => new ComponentPropertyViewModel(host, entityId, component.Index, property))
            .ToArray();
        RemoveCommand = new RelayCommand(remove);
    }

    public string Name { get; }
    public IReadOnlyList<ComponentPropertyViewModel> Properties { get; }
    public ICommand RemoveCommand { get; }
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
