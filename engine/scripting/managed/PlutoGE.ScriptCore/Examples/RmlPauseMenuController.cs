using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Document-driven pause menu. Attach to any active entity and assign an
/// RmlUi canvas to UI/pause-menu.rml.
/// </summary>
public sealed class RmlPauseMenuController : ScriptBehaviour
{
    [SerializedField] private string documentPath = "UI/pause-menu.rml";
    [SerializedField] private string mainMenuScene = "Scenes/MainMenu.plutoscene";
    [SerializedField] private string restartScene = "Scenes/Game.plutoscene";
    [SerializedField] private bool startHidden = true;
    [SerializedField] private bool pauseSimulation = true;

    private RmlDocument? _document;
    private bool _paused;
    private float _previousTimeScale = 1.0f;
    private bool _domReady;

    public override void OnCreate()
    {
        Debug.Log($"Loading RmlDocument at path '{documentPath}'");
        _document = new RmlDocument(documentPath);
        if (_document is null)
        {
            Debug.LogError($"Failed to load RmlDocument at path '{documentPath}'");
            return;
        }
        _document.OnClick("resume", Resume);
        _document.OnClick("restart", Restart);
        _document.OnClick("main-menu", OpenMainMenu);
        _document.OnClick("quit", Quit);

        if (startHidden) SetPaused(false, true);
        else SetPaused(true, true);
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!_domReady && _document is not null)
            _domReady = _paused ? _document.Show() : _document.Hide();

        if (Input.IsKeyPressed(KeyCode.Escape))
        {
            SetPaused(!_paused);
            Debug.Log(_paused ? "Pausing game" : "Resuming game");
        }

    }

    public override void OnDestroy()
    {
        if (_paused)
        {
            if (pauseSimulation) GamePause.TimeScale = _previousTimeScale;
            GamePause.IsPaused = false;
        }
        _document?.Dispose();
    }

    private void Resume()
    {
        if (!_paused) return;
        SetPaused(false);
        Debug.Log("Resuming game");
    }

    private void Restart()
    {
        if (!_paused || string.IsNullOrWhiteSpace(restartScene)) return;
        SetPaused(false);
        SceneManager.LoadScene(restartScene);
    }

    private void OpenMainMenu()
    {
        if (!_paused || string.IsNullOrWhiteSpace(mainMenuScene)) return;
        SetPaused(false);
        SceneManager.LoadScene(mainMenuScene);
    }

    private void Quit()
    {
        if (!_paused) return;
        Debug.Log("Quitting game");
        Application.Quit();
    }

    private void SetPaused(bool paused, bool force = false)
    {
        if (!force && _paused == paused) return;
        _paused = paused;
        GamePause.IsPaused = paused;

        if (paused)
        {
            _previousTimeScale = GamePause.TimeScale;
            if (pauseSimulation) GamePause.TimeScale = 0.0f;
            Input.CursorLocked = false;
            _document?.Show();
        }
        else
        {
            if (pauseSimulation) GamePause.TimeScale = _previousTimeScale;
            _document?.Hide();
            Input.CursorLocked = true;
        }
    }
}
