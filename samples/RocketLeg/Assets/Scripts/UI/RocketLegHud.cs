using System;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts.UI;

/// <summary>
/// The match HUD, built in the native UI on this entity's canvas: scoreboard
/// and clock, both players' boost meters, centre banners, the event feed, the
/// controls panel and the end-of-match card. It presents
/// <see cref="RocketLegMatch"/> state and events and never changes the match.
/// </summary>
public sealed class RocketLegHud : ScriptBehaviour
{
    [SerializedField] private GameObject? match;
    [SerializedField] private bool showHelpOnStart = true;
    // An unprompted controls panel closes itself after this much live play.
    [SerializedField] private float helpAutoCloseSeconds = 10.0f;
    [SerializedField, InputMappingAsset] private string inputMappingAsset = "project://Input/RocketLeg.plutoinput";

    private RocketLegMatch? _match;
    private InputActionMap? _inputActions;
    private ScoreboardView? _scoreboard;
    private BoostMeterView? _blueBoost;
    private BoostMeterView? _orangeBoost;
    private BannerView? _banner;
    private EventFeedView? _feed;
    private HelpPanelView? _help;
    private MatchResultView? _result;
    private float _helpTimer = -1.0f;

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

        match ??= GameObject.Find("Match Manager");
        _match = match?.GetComponent<RocketLegMatch>();
        if (_match is null)
        {
            Debug.LogError("RocketLegHud needs a Match Manager with a RocketLegMatch script.");
            return;
        }

        var root = HudScreen.Create(GameObject).Root;
        _scoreboard = new ScoreboardView(root);
        _blueBoost = new BoostMeterView(root, Team.Blue, 1, Anchors.BottomLeft);
        _orangeBoost = new BoostMeterView(root, Team.Orange, 2, Anchors.BottomRight);
        _feed = new EventFeedView(root);
        _banner = new BannerView(root);
        _help = new HelpPanelView(root) { IsOpen = showHelpOnStart };
        if (showHelpOnStart) _helpTimer = MathF.Max(helpAutoCloseSeconds, 0.0f);
        _result = new MatchResultView(root);

        _match.PhaseChanged += OnPhaseChanged;
        _match.GoalScored += OnGoalScored;
        _match.Demolition += OnDemolition;
    }

    public override void OnDestroy()
    {
        if (_match is null) return;
        _match.PhaseChanged -= OnPhaseChanged;
        _match.GoalScored -= OnGoalScored;
        _match.Demolition -= OnDemolition;
    }

    public override void OnUpdate(float deltaTime)
    {
        if (_match is null) return;
        var snapshot = _match.Snapshot;
        UpdateHelp(snapshot, deltaTime);
        if (snapshot.Phase == MatchPhase.Countdown) _banner?.ShowCountdown(snapshot.CountdownSeconds, snapshot.Overtime);
        _scoreboard?.Update(snapshot, deltaTime);
        _blueBoost?.Update(_match.Car(Team.Blue));
        _orangeBoost?.Update(_match.Car(Team.Orange));
        _banner?.Update(deltaTime);
        _feed?.Update(deltaTime);
    }

    private void UpdateHelp(in MatchSnapshot snapshot, float deltaTime)
    {
        if (_help is null) return;
        if (_inputActions?.WasPressed("ToggleHelp") == true)
        {
            _help.IsOpen = !_help.IsOpen;
            _helpTimer = -1.0f;
        }
        if (_helpTimer < 0.0f || snapshot.Phase != MatchPhase.Playing) return;
        _helpTimer -= deltaTime;
        if (_helpTimer < 0.0f) _help.IsOpen = false;
    }

    private void OnPhaseChanged(MatchPhase phase)
    {
        if (_match is null) return;
        switch (phase)
        {
            case MatchPhase.Countdown:
                _result?.Hide();
                if (_match.Snapshot is { BlueScore: 0, OrangeScore: 0, Overtime: false }) _feed?.Clear();
                break;
            case MatchPhase.Playing:
                _banner?.ShowGo();
                break;
            case MatchPhase.MatchOver:
                _banner?.Hide();
                _result?.Show(_match.Snapshot, _match);
                break;
        }
    }

    private void OnGoalScored(GoalEvent goal)
    {
        _banner?.ShowGoal(goal);
        _scoreboard?.Pop(goal.Team);
        var who = HudText.Credit(goal);
        _feed?.Push(goal.OwnGoal ? $"{who}  OWN GOAL" : $"{who}  SCORED", goal.Team);
    }

    private void OnDemolition(DemolitionEvent demolition) =>
        _feed?.Push($"{HudText.PlayerName(demolition.Attacker)}  DEMOLISHED  {HudText.PlayerName(demolition.Victim)}",
                    demolition.Attacker.Team);
}
