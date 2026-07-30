using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>An entity-owned RML document selected in the editor.</summary>
public sealed class RmlWidgetComponent : ComponentReference
{
    private RmlDocument? _document;
    private string _documentSource = string.Empty;

    internal RmlWidgetComponent(uint entityId) : base(entityId) {}

    internal override ScriptBridge.NativeComponentType ComponentType =>
        ScriptBridge.NativeComponentType.RmlWidget;

    public string Source
    {
        get => ScriptBridge.GetRmlWidgetSource(EntityId);
        set => ScriptBridge.SetRmlWidgetSource(EntityId, value);
    }

    public bool Visible
    {
        get => ScriptBridge.GetRmlWidgetVisible(EntityId);
        set => ScriptBridge.SetRmlWidgetVisible(EntityId, value);
    }

    public RmlDocument Document
    {
        get
        {
            var source = Source;
            if (string.IsNullOrWhiteSpace(source))
                throw new InvalidOperationException("The RML Widget does not have a Source.");
            if (_document is null || _documentSource != source)
            {
                _document?.Dispose();
                _document = new RmlDocument(source);
                _documentSource = source;
            }
            return _document;
        }
    }
    public RmlElement Element(string id) => Document.Element(id);
    public RmlEvent OnClick(string elementId, Action action) =>
        Document.OnClick(elementId, action);
}
