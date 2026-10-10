using System.Globalization;
using PlutoGE.ScriptCore.Authoring;

namespace PlutoGE.ScriptCore.Gameplay;

/// <summary>Template menu with settings, localization and save slots. Replace or extend in project code.</summary>
public sealed class StarterUIBehaviour : ScriptBehaviour
{
    [SerializedField] public bool ApplicationMode;
    [SerializedField] public string Document = "UI/starter.rml";
    [SerializedField] public string Slot = "quick";
    private RmlDocument? _document;
    private RmlViewBinder? _binder;
    private readonly ActionRegistry _actions = new();
    private readonly List<IDisposable> _registrations = [];
    private readonly LocalizationCatalog _strings = new();
    private bool _ready, _menu;
    private float _previousTimeScale = 1;
    private readonly Preferences _preferences = new();
    private string _message = "";

    private sealed class Preferences
    {
        public float InterfaceScale { get; set; } = 1;
        public string Language { get; set; } = "en";
    }

    public override void OnCreate()
    {
        _strings.LoadJson("en", "{\"title\":\"Project menu\",\"continue\":\"Continue\",\"save\":\"Save\",\"load\":\"Load\",\"quit\":\"Quit\"}");
        _strings.LoadJson("fr", "{\"title\":\"Menu du projet\",\"continue\":\"Continuer\",\"save\":\"Enregistrer\",\"load\":\"Charger\",\"quit\":\"Quitter\"}");
        try
        {
            if (ProjectStorage.UserDataFileExists("Settings/interface.json"))
            {
                var saved = ProjectStorage.ReadUserDataJson<Preferences>("Settings/interface.json");
                if (saved is not null && float.IsFinite(saved.InterfaceScale) && saved.InterfaceScale >= 0.5f && saved.InterfaceScale <= 3)
                {
                    _preferences.InterfaceScale = saved.InterfaceScale;
                    _preferences.Language = CultureInfo.GetCultureInfo(saved.Language).Name;
                }
            }
        }
        catch (Exception error) when (error is IOException or System.Text.Json.JsonException or ArgumentException)
        { Debug.LogWarning($"Cannot read interface preferences: {error.Message}"); }
        _strings.Language = _preferences.Language;
        if (UISettings.IsSupported) UISettings.TrySetInterfaceScale(_preferences.InterfaceScale);
        _document = new RmlDocument(Document);
        _binder = new RmlViewBinder();
        Register("continue", () => SetMenu(ApplicationMode));
        Register("save", Save);
        Register("load", Load);
        Register("quit", Application.Quit);
        Register("english", () => SetLanguage("en"));
        Register("french", () => SetLanguage("fr"));
        foreach (var key in new[] { "title", "continue", "save", "load", "quit" })
            _binder.BindLocalizedText(_document.Element(key), _strings, key);
        _binder.BindText(_document.Element("message"), () => _message);
        _binder.BindValue(_document.Element("interface-scale"), () => _preferences.InterfaceScale, value =>
        {
            if (!float.IsFinite(value) || value < 0.5f || value > 3) return;
            _preferences.InterfaceScale = value;
            if (UISettings.IsSupported) UISettings.TrySetInterfaceScale(value);
            PersistPreferences();
        }, text => float.Parse(text, CultureInfo.InvariantCulture));
        _menu = ApplicationMode;
    }

    private void Register(string name, Action action)
    {
        _registrations.Add(_actions.Register(name, action));
        _binder!.BindAction(_document!.Element(name), _actions, name);
    }
    private void SetLanguage(string language)
    {
        _strings.Language = language;
        _preferences.Language = language;
        PersistPreferences();
    }
    private void PersistPreferences()
    {
        try { ProjectStorage.WriteUserDataJson("Settings/interface.json", _preferences); }
        catch (IOException error) { _message = error.Message; }
    }
    public override void OnUpdate(float deltaTime)
    {
        if (_document is null) return;
        if (!_ready && _document.Show()) { _ready = true; SetMenu(_menu); _binder?.Refresh(true); }
        if (!ApplicationMode && Input.IsKeyPressed(KeyCode.Escape)) SetMenu(!_menu);
        if (Input.IsKeyPressed(KeyCode.F6)) Save();
        if (Input.IsKeyPressed(KeyCode.F9)) Load();
        _binder?.Refresh();
    }
    private void SetMenu(bool shown)
    {
        if (!ApplicationMode && shown != GamePause.IsPaused)
        {
            if (shown) { _previousTimeScale = GamePause.TimeScale; GamePause.TimeScale = 0; }
            else GamePause.TimeScale = _previousTimeScale;
            GamePause.IsPaused = shown;
        }
        _menu = shown;
        if (shown) _document?.Show(); else _document?.Hide();
        Input.CursorLocked = !shown;
        _binder?.Refresh(true);
    }
    private void WithSaveService(Action<SaveGameService> operation)
    {
        var service = new SaveGameService(new ProjectSaveStore());
        var registrations = new List<IDisposable>();
        try
        {
            foreach (var entity in GameObject.FindByTag("Persistent"))
                if (entity.GetComponent<PersistentObjectBehaviour>() is { } participant) registrations.Add(service.Register(participant));
            operation(service);
        }
        catch (Exception error) when (error is IOException or System.Text.Json.JsonException or InvalidOperationException or ArgumentException or AggregateException)
        { _message = error.Message; Debug.LogWarning(error.Message); }
        finally { foreach (var registration in registrations) registration.Dispose(); }
    }
    public void Save() => WithSaveService(service =>
    {
        service.Save(Slot, SceneManager.GetActiveScene().Path);
        _message = "Saved.";
    });
    public void Load() => WithSaveService(service =>
    {
        var snapshot = service.Read(Slot);
        if (snapshot.Scene != SceneManager.GetActiveScene().Path) throw new InvalidDataException("Open the saved scene before loading this slot.");
        service.Restore(snapshot);
        _message = "Loaded.";
    });
    public override void OnDestroy()
    {
        if (!ApplicationMode && _menu) { GamePause.TimeScale = _previousTimeScale; GamePause.IsPaused = false; }
        _binder?.Dispose(); _binder = null;
        foreach (var registration in _registrations) registration.Dispose();
        _registrations.Clear();
        _document?.Dispose(); _document = null;
        _ready = false;
    }
}
