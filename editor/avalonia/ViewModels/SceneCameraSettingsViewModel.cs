using System.Collections.ObjectModel;
using System.Globalization;
using System.Windows.Input;
using PlutoGE.Editor.Avalonia.Native;

namespace PlutoGE.Editor.Avalonia.ViewModels;

internal sealed partial class SceneCameraSettingsViewModel : ObservableObject
{
    private readonly EngineHost? _host;
    private string? _selectedEffectType;

    internal SceneCameraSettingsViewModel(EngineHost host)
    {
        _host = host;
        AddEffectCommand = new RelayCommand(AddSelectedEffect, () => !string.IsNullOrWhiteSpace(SelectedEffectType));
        RefreshPostProcessing();
    }

    public ObservableCollection<string> RegisteredEffectTypes { get; } = [];
    public ObservableCollection<PostProcessEffectViewModel> PostProcessEffects { get; } = [];
    public ICommand AddEffectCommand { get; } = new RelayCommand(() => { }, () => false);

    public string? SelectedEffectType
    {
        get => _selectedEffectType;
        set
        {
            if (!SetProperty(ref _selectedEffectType, value)) return;
            if (AddEffectCommand is RelayCommand command) command.RaiseCanExecuteChanged();
        }
    }

    internal void RefreshPostProcessing()
    {
        if (_host is null) return;
        try
        {
            RegisteredEffectTypes.Clear();
            foreach (var type in _host.ReadRegisteredPostProcessTypes()) RegisteredEffectTypes.Add(type);
            SelectedEffectType ??= RegisteredEffectTypes.FirstOrDefault();

            PostProcessEffects.Clear();
            var effects = _host.ReadEditorCameraPostProcessEffects();
            foreach (var effect in effects)
                PostProcessEffects.Add(new PostProcessEffectViewModel(_host, this, effect, effects.Count));
        }
        catch (Exception exception)
        {
            _host.ReportStatus(exception.Message);
        }
    }

    internal void MoveEffect(uint index, int offset)
    {
        if (_host is null) return;
        var target = (int)index + offset;
        if (target < 0 || target >= PostProcessEffects.Count) return;
        TryChange(() => _host.MoveEditorCameraPostProcessEffect(index, (uint)target));
    }

    internal void RemoveEffect(uint index)
    {
        if (_host is null) return;
        TryChange(() => _host.RemoveEditorCameraPostProcessEffect(index));
    }

    private void AddSelectedEffect()
    {
        if (_host is null || string.IsNullOrWhiteSpace(SelectedEffectType)) return;
        TryChange(() => _host.AddEditorCameraPostProcessEffect(SelectedEffectType));
    }

    private void TryChange(Action change)
    {
        try
        {
            change();
            RefreshPostProcessing();
        }
        catch (Exception exception)
        {
            _host?.ReportStatus(exception.Message);
        }
    }
}

internal sealed class PostProcessEffectViewModel : ObservableObject
{
    private readonly EngineHost _host;
    private bool _enabled;

    internal PostProcessEffectViewModel(
        EngineHost host, SceneCameraSettingsViewModel owner, EditorCameraPostProcessEffect effect, int effectCount)
    {
        _host = host;
        Index = effect.Index;
        TypeName = effect.TypeName;
        DisplayName = effect.DisplayName;
        _enabled = effect.Enabled;
        Parameters = effect.Parameters
            .Select(parameter => new PostProcessParameterViewModel(host, effect.Index, parameter))
            .ToArray();
        MoveUpCommand = new RelayCommand(() => owner.MoveEffect(Index, -1), () => Index > 0);
        MoveDownCommand = new RelayCommand(() => owner.MoveEffect(Index, 1), () => Index + 1 < effectCount);
        RemoveCommand = new RelayCommand(() => owner.RemoveEffect(Index));
    }

    public uint Index { get; }
    public string TypeName { get; }
    public string DisplayName { get; }
    public IReadOnlyList<PostProcessParameterViewModel> Parameters { get; }
    public ICommand MoveUpCommand { get; }
    public ICommand MoveDownCommand { get; }
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
                _host.SetEditorCameraPostProcessEffectEnabled(Index, value);
            }
            catch (Exception exception)
            {
                _enabled = previous;
                OnPropertyChanged();
                _host.ReportStatus(exception.Message);
            }
        }
    }
}

internal sealed class PostProcessParameterViewModel : ObservableObject
{
    private readonly EngineHost _host;
    private readonly uint _effectIndex;
    private readonly uint _parameterIndex;
    private string _value;
    private decimal _numericValue;
    private bool _boolValue;
    private string? _selectedOption;

    internal PostProcessParameterViewModel(EngineHost host, uint effectIndex, PostProcessParameterValue parameter)
    {
        _host = host;
        _effectIndex = effectIndex;
        _parameterIndex = parameter.Index;
        Name = parameter.Name;
        Type = parameter.Type;
        EnumOptions = parameter.EnumOptions;
        _value = parameter.Value;
        _boolValue = string.Equals(parameter.Value, "true", StringComparison.OrdinalIgnoreCase) || parameter.Value == "1";
        decimal.TryParse(parameter.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out _numericValue);
        _selectedOption = ResolveEnumOption(parameter.Value, parameter.EnumOptions);
    }

    public string Name { get; }
    public int Type { get; }
    public IReadOnlyList<string> EnumOptions { get; }
    public bool IsNumeric => Type is 0 or 1;
    public bool IsInteger => Type == 1;
    public decimal NumericIncrement => IsInteger ? 1m : 0.1m;
    public bool IsBoolean => Type == 3;
    public bool IsEnum => Type == 4;
    public bool IsText => Type == 2;

    public string Value
    {
        get => _value;
        set
        {
            var previous = _value;
            if (!SetProperty(ref _value, value ?? string.Empty)) return;
            if (!Commit(_value)) { _value = previous; OnPropertyChanged(); }
        }
    }

    public decimal NumericValue
    {
        get => _numericValue;
        set
        {
            var previous = _numericValue;
            if (!SetProperty(ref _numericValue, value)) return;
            var serialized = IsInteger
                ? decimal.Truncate(value).ToString(CultureInfo.InvariantCulture)
                : value.ToString(CultureInfo.InvariantCulture);
            _value = serialized;
            if (!Commit(serialized)) { _numericValue = previous; OnPropertyChanged(); }
        }
    }

    public bool BoolValue
    {
        get => _boolValue;
        set
        {
            var previous = _boolValue;
            if (!SetProperty(ref _boolValue, value)) return;
            _value = value ? "true" : "false";
            if (!Commit(_value)) { _boolValue = previous; OnPropertyChanged(); }
        }
    }

    public string? SelectedOption
    {
        get => _selectedOption;
        set
        {
            var previous = _selectedOption;
            if (value is null || !SetProperty(ref _selectedOption, value)) return;
            var serialized = GetEnumOptionIndex(value, EnumOptions).ToString(CultureInfo.InvariantCulture);
            _value = serialized;
            if (!Commit(serialized)) { _selectedOption = previous; OnPropertyChanged(); }
        }
    }

    private bool Commit(string value)
    {
        try
        {
            _host.SetEditorCameraPostProcessParameter(_effectIndex, _parameterIndex, value);
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
