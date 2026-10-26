using System.Numerics;

namespace PlutoGE.ScriptCore;

public interface IReadOnlyObservable<out T>
{
    T Value { get; }
    event Action<T>? Changed;
}

public sealed class ObservableValue<T> : IReadOnlyObservable<T>
{
    private T _value;
    public ObservableValue(T value = default!) => _value = value;

    public T Value
    {
        get => _value;
        set
        {
            if (EqualityComparer<T>.Default.Equals(_value, value))
                return;
            _value = value;
            Changed?.Invoke(value);
        }
    }

    public event Action<T>? Changed;
}

public sealed class UIBinding<T> : IDisposable
{
    private readonly IReadOnlyObservable<T> _source;
    private readonly Action<T> _apply;
    private bool _disposed;

    public UIBinding(IReadOnlyObservable<T> source, Action<T> apply)
    {
        _source = source ?? throw new ArgumentNullException(nameof(source));
        _apply = apply ?? throw new ArgumentNullException(nameof(apply));
        _source.Changed += OnChanged;
        _apply(_source.Value);
    }

    private void OnChanged(T value) => _apply(value);

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _source.Changed -= OnChanged;
    }
}

public sealed class UIBindingGroup : IDisposable
{
    private readonly List<IDisposable> _bindings = [];

    public T Add<T>(T binding) where T : IDisposable
    {
        _bindings.Add(binding);
        return binding;
    }

    public void Clear()
    {
        foreach (var binding in _bindings)
            binding.Dispose();
        _bindings.Clear();
    }

    public void Dispose() => Clear();
}

public static class UIBind
{
    public static UIBinding<string> Text(UITextComponent target, IReadOnlyObservable<string> source) =>
        new(source, value => target.Text = value ?? string.Empty);

    public static UIBinding<float> Opacity(RectTransformComponent target, IReadOnlyObservable<float> source) =>
        new(source, value => target.Opacity = Math.Clamp(value, 0.0f, 1.0f));

    public static UIBinding<float> Fill(UIImageComponent target, IReadOnlyObservable<float> source) =>
        new(source, value => target.FillAmount = Math.Clamp(value, 0.0f, 1.0f));

    public static UIBinding<Vector3> ImageColor(UIImageComponent target, IReadOnlyObservable<Vector3> source) =>
        new(source, value => target.Color = value);

    public static UIBinding<Vector3> TextColor(UITextComponent target, IReadOnlyObservable<Vector3> source) =>
        new(source, value => target.Color = value);

    public static UIBinding<TSource> Convert<TSource, TTarget>(
        IReadOnlyObservable<TSource> source, Func<TSource, TTarget> convert, Action<TTarget> apply) =>
        new(source, value => apply(convert(value)));

    // Two-way bindings for native controls: the control shows the value, and user edits write it back.

    public static IDisposable Value(UISliderComponent slider, ObservableValue<float> source) =>
        new TwoWayBinding<float>(source, value => slider.Value = value,
                                 handler => slider.ValueChanged += handler, handler => slider.ValueChanged -= handler);

    public static IDisposable IsOn(UIToggleComponent toggle, ObservableValue<bool> source) =>
        new TwoWayBinding<bool>(source, value => toggle.IsOn = value,
                                handler => toggle.ValueChanged += handler, handler => toggle.ValueChanged -= handler);

    public static IDisposable Text(UIInputFieldComponent field, ObservableValue<string> source) =>
        new TwoWayBinding<string>(source, value => field.Text = value ?? string.Empty,
                                  handler => field.ValueChanged += handler, handler => field.ValueChanged -= handler);

    public static IDisposable Selection(UIDropdownComponent dropdown, ObservableValue<int> source) =>
        new TwoWayBinding<int>(source, value => dropdown.Value = value,
                               handler => dropdown.ValueChanged += handler, handler => dropdown.ValueChanged -= handler);

    /// <summary>Text showing a value through a format string, such as "Score: {0}".</summary>
    public static UIBinding<T> Format<T>(UITextComponent target, IReadOnlyObservable<T> source, string format) =>
        new(source, value => target.Text = string.Format(System.Globalization.CultureInfo.CurrentCulture, format, value));
}

/// <summary>Keeps a control and an observable value in step in both directions.</summary>
public sealed class TwoWayBinding<T> : IDisposable
{
    private readonly ObservableValue<T> _source;
    private readonly Action<T> _applyToControl;
    private readonly Action<Action<T>> _unsubscribe;
    private readonly Action<T> _fromControl;
    private bool _disposed;

    public TwoWayBinding(ObservableValue<T> source, Action<T> applyToControl,
                         Action<Action<T>> subscribe, Action<Action<T>> unsubscribe)
    {
        _source = source ?? throw new ArgumentNullException(nameof(source));
        _applyToControl = applyToControl ?? throw new ArgumentNullException(nameof(applyToControl));
        _unsubscribe = unsubscribe ?? throw new ArgumentNullException(nameof(unsubscribe));
        _fromControl = value => _source.Value = value;
        _source.Changed += _applyToControl;
        subscribe(_fromControl);
        _applyToControl(_source.Value);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _source.Changed -= _applyToControl;
        _unsubscribe(_fromControl);
    }
}

/// <summary>
/// Reuses view instances while a collection changes. Creation and parenting are
/// supplied by game code, so this works with prefabs as well as pooled entities.
/// </summary>
public sealed class UIRepeater<T> : IDisposable
{
    private readonly Func<T, GameObject?> _create;
    private readonly Action<GameObject, T, int> _bind;
    private readonly List<GameObject> _views = [];

    public UIRepeater(Func<T, GameObject?> create, Action<GameObject, T, int> bind)
    {
        _create = create;
        _bind = bind;
    }

    public IReadOnlyList<GameObject> Views => _views;

    public void SetItems(IReadOnlyList<T> items)
    {
        for (var index = 0; index < items.Count; ++index)
        {
            if (index >= _views.Count)
            {
                var view = _create(items[index]);
                if (view is null) break;
                _views.Add(view);
            }
            _views[index].Active = true;
            _bind(_views[index], items[index], index);
        }
        for (var index = items.Count; index < _views.Count; ++index)
            _views[index].Active = false;
    }

    public void Dispose()
    {
        foreach (var view in _views)
            if (view.IsValid) view.Destroy();
        _views.Clear();
    }
}
