using System.Globalization;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>A runtime RML document selected by a CanvasComponent document path.</summary>
public sealed class RmlDocument
{
    public string Path { get; }

    public RmlDocument(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        Path = path;
    }

    public bool Show() => ScriptBridge.RmlShowDocument(Path, true);
    public bool Hide() => ScriptBridge.RmlShowDocument(Path, false);
    public bool Reload() => ScriptBridge.RmlReloadDocument(Path);
    public RmlElement Element(string id) => new(this, id);
}

/// <summary>
/// Stable ID-based reference. It remains valid across document hot reloads
/// because the native element is looked up for every operation.
/// </summary>
public sealed class RmlElement
{
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
        var result = new RmlEvent(this, eventName);
        result.EnsureSubscribed();
        return result;
    }
}

/// <summary>An allocation-free event subscription polled during script update.</summary>
public sealed class RmlEvent
{
    public RmlElement Element { get; }
    public string Name { get; }

    internal RmlEvent(RmlElement element, string name)
    {
        Element = element;
        Name = name;
    }

    internal bool EnsureSubscribed() =>
        ScriptBridge.RmlSubscribeEvent(Element.Document.Path, Element.Id, Name);

    public bool Consume() =>
        ScriptBridge.RmlConsumeEvent(Element.Document.Path, Element.Id, Name);
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
