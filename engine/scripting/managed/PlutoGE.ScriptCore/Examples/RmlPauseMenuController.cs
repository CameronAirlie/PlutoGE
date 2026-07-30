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
    private RmlEvent? _resumeClicked;
    private RmlEvent? _restartClicked;
    private RmlEvent? _mainMenuClicked;
    private bool _paused;
    private float _previousTimeScale = 1.0f;
    private bool _domReady;

    public override void OnCreate()
    {
        _document = new RmlDocument(documentPath);
        _resumeClicked = _document.Element("resume").Subscribe("click");
        _restartClicked = _document.Element("restart").Subscribe("click");
        _mainMenuClicked = _document.Element("main-menu").Subscribe("click");

        if (startHidden) SetPaused(false, true);
        else SetPaused(true, true);
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!_domReady && _document is not null)
            _domReady = _paused ? _document.Show() : _document.Hide();

        if (Input.IsKeyPressed(KeyCode.Escape))
            SetPaused(!_paused);

        if (!_paused) return;
        if (_resumeClicked?.Consume() == true)
            SetPaused(false);
        else if (_restartClicked?.Consume() == true && !string.IsNullOrWhiteSpace(restartScene))
        {
            SetPaused(false);
            SceneManager.LoadScene(restartScene);
        }
        else if (_mainMenuClicked?.Consume() == true && !string.IsNullOrWhiteSpace(mainMenuScene))
        {
            SetPaused(false);
            SceneManager.LoadScene(mainMenuScene);
        }
    }

    public override void OnDestroy()
    {
        if (_paused)
        {
            if (pauseSimulation) GamePause.TimeScale = _previousTimeScale;
            GamePause.IsPaused = false;
        }
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
