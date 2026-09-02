using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>Owns scoring, kickoffs, win state, match reset, and HUD text.</summary>
public sealed class RocketLegMatch : ScriptBehaviour
{
    [SerializedField] private GameObject? ball;
    [SerializedField] private GameObject? blueCar;
    [SerializedField] private GameObject? orangeCar;
    [SerializedField] private string documentPath = "UI/rocketleg-hud.rml";
    [SerializedField] private float goalLine = 22.5f;
    [SerializedField] private float goalHalfWidth = 6.0f;
    [SerializedField] private float kickoffDelay = 1.5f;
    [SerializedField] private int winningScore = 5;
    [SerializedField, InputMappingAsset] private string inputMappingAsset = "project://Input/RocketLeg.plutoinput";

    private readonly Vector3 _blueSpawn = new(-12.0f, 0.75f, 0.0f);
    private readonly Vector3 _orangeSpawn = new(12.0f, 0.75f, 0.0f);
    private readonly Vector3 _blueRotation = new(0.0f, -90.0f, 0.0f);
    private readonly Vector3 _orangeRotation = new(0.0f, 90.0f, 0.0f);
    private RocketBall? _ballScript;
    private ArcadeCarController? _blueController;
    private ArcadeCarController? _orangeController;
    private RmlDocument? _hudDocument;
    private InputActionMap? _inputActions;
    private RmlElement? _blueScoreLabel;
    private RmlElement? _orangeScoreLabel;
    private RmlElement? _statusLabel;
    private bool _hudReady;
    private string _status = "KICKOFF!";
    private int _blueScore;
    private int _orangeScore;
    private float _kickoffTimer;
    private bool _roundFrozen;
    private bool _matchOver;

    public override void OnCreate()
    {
        if (!string.IsNullOrWhiteSpace(inputMappingAsset))
        {
            try { _inputActions = InputActionMap.Load(inputMappingAsset); }
            catch (Exception exception)
            {
                Debug.LogError($"Unable to load RocketLeg input map '{inputMappingAsset}': {exception.Message}");
            }
        }

        ball ??= GameObject.Find("Ball");
        blueCar ??= GameObject.Find("Blue Car");
        orangeCar ??= GameObject.Find("Orange Car");
        _ballScript = ball?.GetComponent<RocketBall>();
        _blueController = blueCar?.GetComponent<ArcadeCarController>();
        _orangeController = orangeCar?.GetComponent<ArcadeCarController>();
        _hudDocument = new RmlDocument(documentPath);
        _blueScoreLabel = _hudDocument.Element("blue-score");
        _orangeScoreLabel = _hudDocument.Element("orange-score");
        _statusLabel = _hudDocument.Element("match-status");

        if (ball is null || blueCar is null || orangeCar is null)
        {
            Debug.LogError("RocketLegMatch could not find Ball, Blue Car, or Orange Car.");
        }

        StartKickoff("KICKOFF!");
        UpdateHud();
    }

    public override void OnDestroy()
    {
        _hudDocument?.Dispose();
        _hudDocument = null;
    }

    public override void OnUpdate(float deltaTime)
    {
        // Canvas documents become live after the first RmlUi context update.
        // Retry once per frame, then publish any values set during OnCreate.
        if (!_hudReady && _hudDocument is not null &&
            _hudDocument.Element("rocketleg-hud").SetClass("runtime-ready", true))
        {
            _hudReady = true;
            UpdateHud();
            PublishStatus();
        }

        if (_inputActions?.WasPressed("RestartMatch") == true)
        {
            ResetMatch();
            return;
        }

        if (_roundFrozen)
        {
            _kickoffTimer -= deltaTime;
            if (!_matchOver && _kickoffTimer <= 0.0f)
            {
                SetFrozen(false);
                SetStatus("GO!");
                _kickoffTimer = -0.75f;
            }
            else if (!_matchOver && _kickoffTimer <= -0.75f)
            {
                SetStatus(string.Empty);
            }
            return;
        }

        if (_kickoffTimer < 0.0f)
        {
            _kickoffTimer -= deltaTime;
            if (_kickoffTimer <= -1.5f) SetStatus(string.Empty);
        }

        if (ball is null) return;
        var position = ball.WorldPosition;
        if (MathF.Abs(position.Z) > goalHalfWidth || position.Y > 7.0f) return;

        if (position.X > goalLine)
        {
            ScoreGoal(blueScored: true);
        }
        else if (position.X < -goalLine)
        {
            ScoreGoal(blueScored: false);
        }
    }

    private void ScoreGoal(bool blueScored)
    {
        if (blueScored) _blueScore++;
        else _orangeScore++;

        UpdateHud();
        var winner = _blueScore >= winningScore ? "BLUE" : _orangeScore >= winningScore ? "ORANGE" : string.Empty;
        if (!string.IsNullOrEmpty(winner))
        {
            _matchOver = true;
            SetFrozen(true);
            SetStatus($"{winner} WINS!  PRESS R TO REMATCH");
            return;
        }

        StartKickoff(blueScored ? "BLUE SCORES!" : "ORANGE SCORES!");
    }

    private void StartKickoff(string message)
    {
        SetFrozen(true);
        _ballScript?.ResetBall();
        _blueController?.ResetCar(_blueSpawn, _blueRotation);
        _orangeController?.ResetCar(_orangeSpawn, _orangeRotation);
        _kickoffTimer = kickoffDelay;
        SetStatus(message);
    }

    private void ResetMatch()
    {
        _blueScore = 0;
        _orangeScore = 0;
        _matchOver = false;
        UpdateHud();
        StartKickoff("NEW MATCH");
    }

    private void SetFrozen(bool frozen)
    {
        _roundFrozen = frozen;
        _ballScript?.SetFrozen(frozen);
        _blueController?.SetFrozen(frozen);
        _orangeController?.SetFrozen(frozen);
    }

    private void UpdateHud()
    {
        if (!_hudReady) return;
        if (_blueScoreLabel is not null) _blueScoreLabel.Markup = _blueScore.ToString();
        if (_orangeScoreLabel is not null) _orangeScoreLabel.Markup = _orangeScore.ToString();
    }

    private void SetStatus(string text)
    {
        _status = text;
        PublishStatus();
    }

    private void PublishStatus()
    {
        if (_hudReady && _statusLabel is not null) _statusLabel.Markup = _status;
    }
}
