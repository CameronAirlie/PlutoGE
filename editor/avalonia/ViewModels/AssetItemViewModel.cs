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

internal sealed record ComponentPropertyViewModel(string Name, string Value);

internal sealed record ComponentViewModel(
    string Name,
    bool Enabled,
    IReadOnlyList<ComponentPropertyViewModel> Properties)
{
    public string StateText => Enabled ? "Enabled" : "Disabled";
    public bool HasProperties => Properties.Count > 0;
}
