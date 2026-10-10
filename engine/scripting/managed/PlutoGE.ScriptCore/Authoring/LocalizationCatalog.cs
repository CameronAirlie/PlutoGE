using System.Globalization;
using System.Text.Json;

namespace PlutoGE.ScriptCore.Authoring;

/// <summary>Project-owned string tables with parent-culture and explicit fallback resolution.</summary>
public sealed class LocalizationCatalog
{
    private readonly Dictionary<string, Dictionary<string, string>> _tables = new(StringComparer.OrdinalIgnoreCase);
    private string _language;
    public string FallbackLanguage { get; }
    public event Action? Changed;
    public string Language
    {
        get => _language;
        set
        {
            var normalized = CultureInfo.GetCultureInfo(value).Name;
            if (_language == normalized) return;
            _language = normalized;
            Changed?.Invoke();
        }
    }

    public LocalizationCatalog(string fallbackLanguage = "en")
    {
        FallbackLanguage = CultureInfo.GetCultureInfo(fallbackLanguage).Name;
        _language = FallbackLanguage;
    }

    /// <summary>Accepts {"key":"translation"}. Loading is explicit so callers can use packed assets or remote sources.</summary>
    public void LoadJson(string language, string json)
    {
        var culture = CultureInfo.GetCultureInfo(language).Name;
        using var document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object) throw new JsonException("A string table must be an object.");
        var table = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var item in document.RootElement.EnumerateObject())
        {
            if (string.IsNullOrWhiteSpace(item.Name) || item.Value.ValueKind != JsonValueKind.String || !table.TryAdd(item.Name, item.Value.GetString()!))
                throw new JsonException($"Invalid or duplicate translation key '{item.Name}'.");
        }
        _tables[culture] = table;
        Changed?.Invoke();
    }

    public bool TryGet(string key, out string text)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(key);
        var culture = CultureInfo.GetCultureInfo(Language);
        while (true)
        {
            if (_tables.TryGetValue(culture.Name, out var table) && table.TryGetValue(key, out text!)) return true;
            if (culture.Equals(CultureInfo.InvariantCulture)) break;
            culture = culture.Parent;
        }
        if (_tables.TryGetValue(FallbackLanguage, out var fallback) && fallback.TryGetValue(key, out text!)) return true;
        text = key;
        return false;
    }

    public string Get(string key) { TryGet(key, out var text); return text; }
    public string Format(string key, params object?[] arguments) => string.Format(CultureInfo.GetCultureInfo(Language), Get(key), arguments);
    public IReadOnlyList<string> MissingKeys(string language)
    {
        var culture = CultureInfo.GetCultureInfo(language).Name;
        if (!_tables.TryGetValue(FallbackLanguage, out var fallback)) return [];
        _tables.TryGetValue(culture, out var table);
        return fallback.Keys.Where(key => table is null || !table.ContainsKey(key)).Order(StringComparer.Ordinal).ToArray();
    }
}
