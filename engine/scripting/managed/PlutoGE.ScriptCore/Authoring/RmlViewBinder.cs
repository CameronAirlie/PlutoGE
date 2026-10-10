using System.Globalization;
using System.Net;
using System.Reflection;

namespace PlutoGE.ScriptCore.Authoring;

/// <summary>Explicit ID/property bindings to public model properties. Does not register native data-model expressions.</summary>
public sealed class RmlViewBinder : IDisposable
{
    private readonly List<Action<bool>> _refresh = [];
    private readonly List<Action> _unsubscribe = [];
    private bool _disposed;
    private readonly string _markerAttribute = "data-pluto-binding-" + Guid.NewGuid().ToString("N");

    public void BindText<T>(RmlElement element, Func<T> read, Func<T, string>? format = null)
    {
        ArgumentNullException.ThrowIfNull(element);
        ArgumentNullException.ThrowIfNull(read);
        AddRefresh(element, (force, previous) =>
        {
            var text = format is null ? Convert.ToString(read(), CultureInfo.InvariantCulture) ?? "" : format(read());
            if (force || text != previous) element.Markup = WebUtility.HtmlEncode(text);
            return text;
        });
    }

    public void BindValue<T>(RmlElement element, Func<T> read, Action<T> write, Func<string, T> parse)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(element);
        ArgumentNullException.ThrowIfNull(read);
        ArgumentNullException.ThrowIfNull(write);
        ArgumentNullException.ThrowIfNull(parse);
        var applying = false;
        AddRefresh(element, (force, previous) =>
        {
            var text = Convert.ToString(read(), CultureInfo.InvariantCulture) ?? "";
            if (force || text != previous)
            {
                applying = true;
                try { element["value"] = text; } finally { applying = false; }
            }
            return text;
        });
        void Changed()
        {
            if (applying) return;
            try
            {
                var value = parse(element["value"]);
                if (!EqualityComparer<T>.Default.Equals(value, read())) write(value);
                // Programmatic value changes can queue another change event.
                // Reapply only if the model normalized/rejected the user's value.
                var canonical = Convert.ToString(read(), CultureInfo.InvariantCulture) ?? "";
                Refresh(element["value"] != canonical);
            }
            catch (FormatException) { Refresh(true); }
            catch (OverflowException) { Refresh(true); }
        }
        var subscription = element.On("change", Changed);
        _unsubscribe.Add(() => subscription.Triggered -= Changed);
    }

    /// <summary>Binds one public readable property; optional two-way binding supports strings, booleans and numeric values.</summary>
    public void BindProperty(RmlElement element, object model, string propertyName, bool twoWay = false)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(model);
        var property = model.GetType().GetProperty(propertyName, BindingFlags.Public | BindingFlags.Instance)
            ?? throw new ArgumentException($"Unknown property '{propertyName}'.", nameof(propertyName));
        if (property.GetMethod?.IsPublic != true || property.GetIndexParameters().Length != 0)
            throw new ArgumentException("Binding requires a public readable non-indexed property.", nameof(propertyName));
        if (!twoWay) { BindText(element, () => property.GetValue(model)); return; }
        var type = property.PropertyType;
        if (property.SetMethod?.IsPublic != true || !(type == typeof(string) || type == typeof(bool) || type == typeof(int) || type == typeof(float) || type == typeof(double)))
            throw new ArgumentException("Two-way properties must be writable strings, booleans or numbers.", nameof(propertyName));
        BindValue(element, () => property.GetValue(model), value => property.SetValue(model, value), text =>
        {
            var value = Convert.ChangeType(text, type, CultureInfo.InvariantCulture);
            if (value is float f && !float.IsFinite(f) || value is double d && !double.IsFinite(d)) throw new FormatException("Non-finite value.");
            return value;
        });
    }

    public void BindAction(RmlElement element, ActionRegistry actions, string actionName)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(element);
        ArgumentNullException.ThrowIfNull(actions);
        ArgumentException.ThrowIfNullOrWhiteSpace(actionName);
        void Clicked() => actions.Invoke(actionName);
        var subscription = element.OnClick(Clicked);
        _unsubscribe.Add(() => subscription.Triggered -= Clicked);
    }

    public void BindLocalizedText(RmlElement element, LocalizationCatalog catalog, string key)
    {
        ArgumentNullException.ThrowIfNull(catalog);
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        BindText(element, () => catalog.Get(key));
    }

    /// <summary>Call in OnUpdate. Force after document creation/reload to reapply values even when the model is unchanged.</summary>
    public void Refresh(bool force = false)
    {
        ThrowIfDisposed();
        foreach (var refresh in _refresh) refresh(force);
    }
    private void AddRefresh(RmlElement element, Func<bool, string?, string> apply)
    {
        ThrowIfDisposed();
        string? previous = null;
        _refresh.Add(force =>
        {
            var recreated = element[_markerAttribute] != "1";
            previous = apply(force || recreated, previous);
            if (recreated) element[_markerAttribute] = "1";
        });
    }
    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        foreach (var unsubscribe in _unsubscribe) unsubscribe();
        _unsubscribe.Clear();
        _refresh.Clear();
    }
}
