using System.Globalization;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>A runtime RML document selected by a CanvasComponent document path.</summary>
public sealed class RmlDocument : IDisposable
{
    private readonly Dictionary<string, RmlElement> _elements = [];
    private bool _enabled = true;
    private bool _visible = true;
    private bool _disposed;

    public string Path { get; }
    public bool Enabled
    {
        get => _enabled;
        set
        {
            ThrowIfDisposed();
            if (_enabled == value) return;
            _enabled = value;
            ScriptBridge.RmlShowDocument(Path, value && _visible);
            EnabledChanged?.Invoke(value);
        }
    }
    public bool Visible
    {
        get => _visible;
        set
        {
            ThrowIfDisposed();
            if (_visible == value) return;
            _visible = value;
            ScriptBridge.RmlShowDocument(Path, _enabled && value);
            VisibilityChanged?.Invoke(value);
        }
    }
    public bool IsShown => Enabled && Visible;
    public event Action<bool>? EnabledChanged;
    public event Action<bool>? VisibilityChanged;

    public RmlDocument(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        Path = path;
    }

    public bool Show()
    {
        ThrowIfDisposed();
        _visible = true;
        var result = !_enabled || ScriptBridge.RmlShowDocument(Path, true);
        VisibilityChanged?.Invoke(true);
        return result;
    }
    public bool Hide()
    {
        ThrowIfDisposed();
        _visible = false;
        var result = ScriptBridge.RmlShowDocument(Path, false);
        VisibilityChanged?.Invoke(false);
        return result;
    }
    public void Enable() => Enabled = true;
    public void Disable() => Enabled = false;
    public void Toggle() => Visible = !Visible;
    public bool Reload()
    {
        ThrowIfDisposed();
        return ScriptBridge.RmlReloadDocument(Path);
    }
    public RmlElement Element(string id)
    {
        ThrowIfDisposed();
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        if (!_elements.TryGetValue(id, out var element))
        {
            element = new RmlElement(this, id);
            _elements.Add(id, element);
        }
        return element;
    }
    public RmlEvent OnClick(string elementId, Action action) =>
        Element(elementId).On("click", action);
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _enabled = false;
        ScriptBridge.RmlShowDocument(Path, false);
        foreach (var element in _elements.Values)
            element.DisableEvents();
        _elements.Clear();
    }
    internal bool CanDispatchEvents => !_disposed && _enabled;
    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);
}

/// <summary>
/// Stable ID-based reference. It remains valid across document hot reloads
/// because the native element is looked up for every operation.
/// </summary>
public sealed class RmlElement
{
    private readonly Dictionary<string, RmlEvent> _events =
        new(StringComparer.OrdinalIgnoreCase);

    public RmlDocument Document { get; }
    public string Id { get; }

    internal RmlElement(RmlDocument document, string id)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        Document = document;
        Id = id;
    }

    public string Markup
    {
        get => ScriptBridge.RmlGetText(Document.Path, Id);
        set => ScriptBridge.RmlSetText(Document.Path, Id, value);
    }

    public string this[string attribute]
    {
        get => ScriptBridge.RmlGetAttribute(Document.Path, Id, attribute);
        set => ScriptBridge.RmlSetAttribute(Document.Path, Id, attribute, value);
    }

    public bool SetClass(string name, bool enabled = true) =>
        ScriptBridge.RmlSetClass(Document.Path, Id, name, enabled);

    public bool SetStyle(string property, string value) =>
        ScriptBridge.RmlSetStyle(Document.Path, Id, property, value);

    public bool SetStyle(string property, float pixels) =>
        SetStyle(property, pixels.ToString(CultureInfo.InvariantCulture) + "px");

    public RmlEvent Subscribe(string eventName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(eventName);
        if (_events.TryGetValue(eventName, out var existing))
            return existing;
        var result = new RmlEvent(this, eventName);
        _events.Add(eventName, result);
        result.EnsureSubscribed();
        return result;
    }
    public RmlEvent On(string eventName, Action action)
    {
        ArgumentNullException.ThrowIfNull(action);
        var result = Subscribe(eventName);
        result.Triggered += action;
        return result;
    }
    public RmlEvent OnClick(Action action) => On("click", action);
    public event Action Clicked
    {
        add => OnClick(value);
        remove
        {
            if (_events.TryGetValue("click", out var click))
                click.Triggered -= value;
        }
    }
    internal void DisableEvents()
    {
        foreach (var @event in _events.Values)
            @event.Enabled = false;
    }
}

/// <summary>An allocation-free event subscription polled during script update.</summary>
public sealed class RmlEvent
{
    private static readonly List<WeakReference<RmlEvent>> RegisteredEvents = [];
    private static ulong _lastDispatchSequence;

    public RmlElement Element { get; }
    public string Name { get; }
    public bool Enabled { get; set; } = true;
    public event Action? Triggered;

    internal RmlEvent(RmlElement element, string name)
    {
        Element = element;
        Name = name;
        lock (RegisteredEvents)
            RegisteredEvents.Add(new WeakReference<RmlEvent>(this));
    }

    internal bool EnsureSubscribed() =>
        ScriptBridge.RmlSubscribeEvent(Element.Document.Path, Element.Id, Name);

    public bool Consume() =>
        ScriptBridge.RmlConsumeEvent(Element.Document.Path, Element.Id, Name);

    internal static void DispatchRegisteredEvents(ulong updateSequence)
    {
        if (updateSequence == 0 || updateSequence == _lastDispatchSequence) return;
        _lastDispatchSequence = updateSequence;
        lock (RegisteredEvents)
        {
            for (var index = RegisteredEvents.Count - 1; index >= 0; --index)
            {
                if (!RegisteredEvents[index].TryGetTarget(out var @event))
                {
                    RegisteredEvents.RemoveAt(index);
                    continue;
                }
                if (@event.Triggered is null)
                    continue;
                @event.EnsureSubscribed();
                while (@event.Consume())
                {
                    if (@event.Enabled && @event.Element.Document.CanDispatchEvents)
                        @event.Triggered.Invoke();
                }
            }
        }
    }
}

public static class RmlBindings
{
    public static UIBinding<string> Markup(RmlElement target, IReadOnlyObservable<string> source) =>
        new(source, value => target.Markup = value);

    public static UIBinding<float> Progress(RmlElement target, IReadOnlyObservable<float> source) =>
        new(source, value => target["value"] = value.ToString(CultureInfo.InvariantCulture));

    public static UIBinding<float> Pixels(RmlElement target, string property, IReadOnlyObservable<float> source) =>
        new(source, value => target.SetStyle(property, value));

    public static UIBinding<bool> Class(RmlElement target, string className, IReadOnlyObservable<bool> source) =>
        new(source, value => target.SetClass(className, value));
}
