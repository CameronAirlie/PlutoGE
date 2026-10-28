using System;
using System.Collections.Generic;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace RocketLeg.Scripts;

/// <summary>
/// Owns the rules of a timed match: kickoff countdowns, the clock (which waits
/// for the first touch after each kickoff), goals, golden-goal overtime,
/// demolition respawns and per-car stats. It has no UI; presenters poll
/// <see cref="Snapshot"/> and subscribe to the events.
/// </summary>
public sealed class RocketLegMatch : ScriptBehaviour
{
    [SerializedField] private GameObject? ball;
    [SerializedField] private GameObject? blueCar;
    [SerializedField] private GameObject? orangeCar;
    [SerializedField] private GameObject? goalExplosion;
    [SerializedField] private float matchSeconds = 300.0f;
    [SerializedField] private float countdownSeconds = 3.0f;
    [SerializedField] private float celebrationSeconds = 3.0f;
    [SerializedField, InputMappingAsset] private string inputMappingAsset = "project://Input/RocketLeg.plutoinput";

    private static readonly Vector3 BallParking = new(0.0f, 200.0f, 0.0f);

    private readonly Dictionary<uint, PlayerStats> _stats = new();
    private readonly Random _random = new();
    private RocketBall? _ball;
    private GoalExplosion? _explosion;
    private ArcadeCarController? _blue;
    private ArcadeCarController? _orange;
    private InputActionMap? _inputActions;
    private MatchPhase _phase = MatchPhase.Countdown;
    private int _blueScore;
    private int _orangeScore;
    private float _clock;
    private bool _overtime;
    private bool _clockRunning;
    private float _phaseTimer;
    private int _respawnSlot;
    private bool _resetPending;
    private Team? _winner;

    public MatchSnapshot Snapshot => new(_phase, _blueScore, _orangeScore, _clock, _overtime, _clockRunning,
                                         _phase == MatchPhase.Countdown ? MathF.Max(_phaseTimer, 0.0f) : 0.0f, _winner);

    public event Action<MatchPhase>? PhaseChanged;
    public event Action<GoalEvent>? GoalScored;
    public event Action<DemolitionEvent>? Demolition;

    public ArcadeCarController? Car(Team team) => team == Team.Blue ? _blue : _orange;

    public PlayerStats Stats(ArcadeCarController car)
    {
        if (!_stats.TryGetValue(car.EntityId, out var stats)) _stats[car.EntityId] = stats = new PlayerStats();
        return stats;
    }

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
        goalExplosion ??= GameObject.Find("Goal Explosion");
        _ball = ball?.GetComponent<RocketBall>();
        _blue = blueCar?.GetComponent<ArcadeCarController>();
        _orange = orangeCar?.GetComponent<ArcadeCarController>();
        _explosion = goalExplosion?.GetComponent<GoalExplosion>();
        if (_ball is null || _blue is null || _orange is null)
        {
            Debug.LogError("RocketLegMatch could not find the Ball, Blue Car and Orange Car scripts.");
            return;
        }

        _ball.Touched += OnBallTouched;
        _blue.Demolished += attacker => OnDemolished(attacker, _blue);
        _orange.Demolished += attacker => OnDemolished(attacker, _orange);
        // Other scripts' OnCreate may not have run yet (cars need their
        // rigidbodies to freeze), so the first kickoff waits for OnUpdate.
        _resetPending = true;
    }

    public override void OnDestroy()
    {
        if (_ball is not null) _ball.Touched -= OnBallTouched;
    }

    public override void OnUpdate(float deltaTime)
    {
        if (_ball is null) return;
        if (_resetPending || _inputActions?.WasPressed("RestartMatch") == true)
        {
            ResetMatch();
            return;
        }

        switch (_phase)
        {
            case MatchPhase.Countdown:
                _phaseTimer -= deltaTime;
                if (_phaseTimer <= 0.0f)
                {
                    SetFrozen(false);
                    SetPhase(MatchPhase.Playing);
                }
                break;
            case MatchPhase.Playing:
                UpdateClock(deltaTime);
                if (_phase == MatchPhase.Playing) DetectGoal();
                break;
            case MatchPhase.GoalScored:
                _phaseTimer -= deltaTime;
                if (_phaseTimer <= 0.0f) AfterGoal();
                break;
        }
    }

    private void UpdateClock(float deltaTime)
    {
        // Contact events need real penetration, so a gentle first push can go
        // unreported; a moving ball means the kickoff was touched either way.
        if (!_clockRunning && _ball!.Velocity.LengthSquared() > 1.0f) _clockRunning = true;
        if (!_clockRunning) return;
        if (_overtime)
        {
            _clock += deltaTime;
            return;
        }

        _clock = MathF.Max(0.0f, _clock - deltaTime);
        // At 0:00 play continues until the ball next touches the floor.
        if (_clock > 0.0f || _ball!.Position.Y > _ball.Radius + 0.25f) return;
        if (_blueScore == _orangeScore) StartOvertime();
        else EndMatch();
    }

    private void DetectGoal()
    {
        var position = _ball!.Position;
        var radius = _ball.Radius;
        // Goal mouths are the planes x = +/-GoalLine; the whole ball must cross.
        if (MathF.Abs(position.Z) > ArenaLayout.GoalHalfWidth || position.Y > ArenaLayout.GoalHeight) return;
        if (position.X > ArenaLayout.GoalLine + radius) ScoreGoal(Team.Blue);
        else if (position.X < -ArenaLayout.GoalLine - radius) ScoreGoal(Team.Orange);
    }

    private void ScoreGoal(Team team)
    {
        if (team == Team.Blue) _blueScore++;
        else _orangeScore++;

        var scorer = _ball!.LastTouch;
        var ownGoal = scorer is not null && scorer.Team != team;
        if (scorer is not null && !ownGoal) Stats(scorer).Goals++;
        var position = _ball.Position;
        var speed = _ball.Velocity.Length();
        _explosion?.Play(position, team);

        // The ball leaves play; cars keep driving through the celebration.
        _ball.SetFrozen(true);
        _ball.ResetBall(BallParking);
        _clockRunning = false;
        _phaseTimer = MathF.Max(celebrationSeconds, 0.0f);
        SetPhase(MatchPhase.GoalScored);
        GoalScored?.Invoke(new GoalEvent(team, scorer, speed, ownGoal, _overtime));
    }

    private void AfterGoal()
    {
        if (_overtime || (_clock <= 0.0f && _blueScore != _orangeScore)) EndMatch();
        else if (_clock <= 0.0f) StartOvertime();
        else StartKickoff();
    }

    private void StartOvertime()
    {
        _overtime = true;
        _clock = 0.0f;
        StartKickoff();
    }

    private void StartKickoff()
    {
        SetFrozen(true);
        _ball!.ResetBall(new Vector3(0.0f, _ball.Radius + 0.05f, 0.0f));
        var slot = _random.Next(KickoffLayout.KickoffCount);
        PlaceForKickoff(_blue!, slot);
        PlaceForKickoff(_orange!, slot);
        foreach (var pad in GameObject.FindByTag("boost-pad")) pad.GetComponent<BoostPad>()?.ResetPad();
        _clockRunning = false;
        _phaseTimer = MathF.Max(countdownSeconds, 0.0f);
        SetPhase(MatchPhase.Countdown);
    }

    private void PlaceForKickoff(ArcadeCarController car, int slot)
    {
        var (position, rotation) = KickoffLayout.Kickoff(car.Team, slot);
        car.ResetCar(position, rotation);
        (car.RespawnPosition, car.RespawnRotation) = KickoffLayout.Respawn(car.Team, _respawnSlot);
    }

    private void ResetMatch()
    {
        _blueScore = 0;
        _orangeScore = 0;
        _overtime = false;
        _winner = null;
        _resetPending = false;
        _clock = MathF.Max(matchSeconds, 1.0f);
        foreach (var stats in _stats.Values) stats.Reset();
        StartKickoff();
    }

    private void EndMatch()
    {
        _winner = _blueScore > _orangeScore ? Team.Blue : _orangeScore > _blueScore ? Team.Orange : null;
        _clockRunning = false;
        SetFrozen(true);
        SetPhase(MatchPhase.MatchOver);
    }

    private void OnBallTouched(ArcadeCarController car, float closingSpeed)
    {
        if (_phase != MatchPhase.Playing) return;
        Stats(car).Touches++;
        _clockRunning = true;
    }

    private void OnDemolished(ArcadeCarController attacker, ArcadeCarController victim)
    {
        (victim.RespawnPosition, victim.RespawnRotation) = KickoffLayout.Respawn(victim.Team, ++_respawnSlot);
        if (_phase == MatchPhase.Playing || _phase == MatchPhase.GoalScored) Stats(attacker).Demolitions++;
        Demolition?.Invoke(new DemolitionEvent(attacker, victim));
    }

    private void SetFrozen(bool frozen)
    {
        _ball?.SetFrozen(frozen);
        _blue?.SetFrozen(frozen);
        _orange?.SetFrozen(frozen);
    }

    private void SetPhase(MatchPhase phase)
    {
        _phase = phase;
        PhaseChanged?.Invoke(phase);
    }
}
