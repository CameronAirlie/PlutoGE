using System;
using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Simple automatic engine audio controller. Attach this to an entity with a SoundEmitterComponent,
/// assign a target vehicle object, and it will use vehicle telemetry for RPM, load, gear, and tyre screech.
/// </summary>
public sealed class EngineSoundController : ScriptBehaviour
{
    [SerializedField] private GameObject? target = null;
    [SerializedField] private GameObject? engineEmitterObject = null;
    [SerializedField] private GameObject? tyreScreechEmitterObject = null;
    [SerializedField] private bool playOnCreate = true;
    [SerializedField] private bool preferVehicleTelemetry = true;
    [SerializedField] private bool usePlanarSpeed = true;
    [SerializedField] private float wheelRadius = 0.34f;
    [SerializedField] private float finalDriveRatio = 3.9f;
    [SerializedField] private float reverseGearRatio = 3.2f;
    [SerializedField] private float firstGearRatio = 3.1f;
    [SerializedField] private float secondGearRatio = 2.2f;
    [SerializedField] private float thirdGearRatio = 1.6f;
    [SerializedField] private float fourthGearRatio = 1.25f;
    [SerializedField] private float fifthGearRatio = 1.0f;
    [SerializedField] private float sixthGearRatio = 0.82f;
    [SerializedField] private float idleRpm = 900.0f;
    [SerializedField] private float redlineRpm = 7200.0f;
    [SerializedField] private float upshiftRpm = 6100.0f;
    [SerializedField] private float downshiftRpm = 1800.0f;
    [SerializedField] private float rpmResponse = 9.0f;
    [SerializedField] private float enginePitchAtIdle = 0.82f;
    [SerializedField] private float enginePitchAtRedline = 1.95f;
    [SerializedField] private float engineVolumeAtIdle = 0.18f;
    [SerializedField] private float engineVolumeAtRedline = 0.95f;
    [SerializedField] private float loadVolumeBoost = 0.2f;
    [SerializedField] private float tyreSlipThreshold = 0.18f;
    [SerializedField] private float tyreSlipFull = 0.95f;
    [SerializedField] private float minimumScreechSpeed = 7.5f;
    [SerializedField] private float wheelSpinScreechThreshold = 2.0f;
    [SerializedField] private float minimumTelemetryScreech = 0.08f;
    [SerializedField] private float screechVolume = 0.9f;
    [SerializedField] private float screechPitchLow = 0.9f;
    [SerializedField] private float screechPitchHigh = 1.3f;
    [SerializedField] private float screechResponse = 10.0f;
    [SerializedField] private bool loopTyreScreech = true;

    private RigidbodyComponent? _rigidbody;
    private SoundEmitterComponent? _engineEmitter;
    private SoundEmitterComponent? _tyreEmitter;
    private float _smoothedRpm;
    private float _smoothedEnginePitch;
    private float _smoothedEngineVolume;
    private float _smoothedScreechAmount;
    private int _currentGear = 1;
    private bool _tyreScreechActive;

    public override void OnCreate()
    {
        var targetObject = target ?? GameObject;
        _rigidbody = targetObject.GetComponent<RigidbodyComponent>();
        _engineEmitter = (engineEmitterObject ?? GameObject).GetComponent<SoundEmitterComponent>();
        _tyreEmitter = tyreScreechEmitterObject?.GetComponent<SoundEmitterComponent>();

        _currentGear = 1;
        _smoothedRpm = MathF.Max(idleRpm, 0.0f);
        _smoothedEnginePitch = Math.Clamp(enginePitchAtIdle, 0.25f, 4.0f);
        _smoothedEngineVolume = MathF.Max(engineVolumeAtIdle, 0.0f);
        _smoothedScreechAmount = 0.0f;

        if (_engineEmitter is not null)
        {
            _engineEmitter.Pitch = _smoothedEnginePitch;
            _engineEmitter.Volume = _smoothedEngineVolume;
            EnsurePlaying(_engineEmitter);
        }

        if (_tyreEmitter is not null)
        {
            _tyreEmitter.Looping = loopTyreScreech;
            _tyreEmitter.Volume = 0.0f;
            _tyreScreechActive = false;
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (_rigidbody is null || _engineEmitter is null || deltaTime <= 0.0f)
        {
            return;
        }

        var targetObject = target ?? GameObject;
        if (preferVehicleTelemetry && RaycastVehicleController.TryGetTelemetry(targetObject, out var telemetry))
        {
            _currentGear = telemetry.Gear;
            var telemetryRpmAmount = 1.0f - MathF.Exp(-MathF.Max(rpmResponse, 0.0f) * deltaTime);
            _smoothedRpm = Lerp(_smoothedRpm, MathF.Max(telemetry.EngineRpm, idleRpm), telemetryRpmAmount);

            var telemetryRpm01 = telemetry.EngineRpm01 > 0.0f
                ? telemetry.EngineRpm01
                : SafeInverseLerp(MathF.Max(idleRpm, 0.0f), MathF.Max(redlineRpm, idleRpm + 100.0f), _smoothedRpm);
            var telemetryLoad01 = Math.Clamp(MathF.Max(telemetry.Throttle, MathF.Abs(telemetry.ForwardSpeed) / MathF.Max(telemetry.Speed, 1.0f)), 0.0f, 1.0f);

            UpdateEngineEmitter(telemetryRpm01, telemetryLoad01, telemetryRpmAmount);
            UpdateTyreScreech(GetTelemetryTyreScreechAmount(telemetry), deltaTime);
            return;
        }

        var velocity = _rigidbody.Velocity;
        var speed = usePlanarSpeed
            ? new Vector2(velocity.X, velocity.Z).Length()
            : velocity.Length();

        var forward = targetObject.Forward;
        var forwardLengthSquared = forward.LengthSquared();
        var forwardDirection = forwardLengthSquared > 0.000001f
            ? Vector3.Normalize(forward)
            : -Vector3.UnitZ;
        var right = targetObject.Right;
        var rightLengthSquared = right.LengthSquared();
        var rightDirection = rightLengthSquared > 0.000001f
            ? Vector3.Normalize(right)
            : Vector3.UnitX;
        var forwardSpeed = Vector3.Dot(velocity, forwardDirection);
        var lateralSpeed = Vector3.Dot(velocity, rightDirection);
        var signedDriveSpeed = usePlanarSpeed ? forwardSpeed : MathF.Sign(forwardSpeed) * speed;

        UpdateGear(signedDriveSpeed);

        var wheelRpm = ComputeWheelRpm(signedDriveSpeed);
        var gearRatio = GetCurrentGearRatio();
        var targetRpm = MathF.Max(idleRpm, MathF.Abs(wheelRpm * gearRatio * MathF.Max(finalDriveRatio, 0.01f)));
        targetRpm = Math.Clamp(targetRpm, MathF.Max(idleRpm, 0.0f), MathF.Max(redlineRpm, idleRpm + 100.0f));

        var rpmAmount = 1.0f - MathF.Exp(-MathF.Max(rpmResponse, 0.0f) * deltaTime);
        _smoothedRpm = Lerp(_smoothedRpm, targetRpm, rpmAmount);

        var rpm01 = SafeInverseLerp(MathF.Max(idleRpm, 0.0f), MathF.Max(redlineRpm, idleRpm + 100.0f), _smoothedRpm);
        var load01 = SafeInverseLerp(0.0f, 1.0f, MathF.Abs(signedDriveSpeed) / MathF.Max(ComputeTopSpeedForGear(_currentGear), 0.01f));
        UpdateEngineEmitter(rpm01, load01, rpmAmount);

        UpdateTyreScreech(speed, MathF.Abs(forwardSpeed), MathF.Abs(lateralSpeed), deltaTime);
    }

    private void UpdateEngineEmitter(float rpm01, float load01, float rpmAmount)
    {
        if (_engineEmitter is null)
        {
            return;
        }

        var clampedRpm01 = Math.Clamp(rpm01, 0.0f, 1.0f);
        var targetPitch = Lerp(Math.Clamp(enginePitchAtIdle, 0.25f, 4.0f), Math.Clamp(enginePitchAtRedline, 0.25f, 4.0f), EaseOut(clampedRpm01));
        var targetVolume = Lerp(Math.Max(engineVolumeAtIdle, 0.0f), Math.Max(engineVolumeAtRedline, 0.0f), MathF.Sqrt(clampedRpm01));
        targetVolume += Math.Clamp(load01, 0.0f, 1.0f) * MathF.Max(loadVolumeBoost, 0.0f);

        _smoothedEnginePitch = Lerp(_smoothedEnginePitch, targetPitch, rpmAmount);
        _smoothedEngineVolume = Lerp(_smoothedEngineVolume, targetVolume, rpmAmount);

        _engineEmitter.Pitch = _smoothedEnginePitch;
        _engineEmitter.Volume = _smoothedEngineVolume;
        EnsurePlaying(_engineEmitter);
    }

    private void UpdateGear(float signedDriveSpeed)
    {
        var forwardSpeed = MathF.Abs(signedDriveSpeed);
        if (signedDriveSpeed < -0.5f)
        {
            _currentGear = -1;
            return;
        }

        if (_currentGear <= 0)
        {
            _currentGear = 1;
        }

        var currentGearRpm = EstimateRpmForGear(forwardSpeed, _currentGear);
        var maxForwardGear = 6;
        if (_currentGear < maxForwardGear && currentGearRpm >= MathF.Max(upshiftRpm, idleRpm + 500.0f))
        {
            _currentGear += 1;
            return;
        }

        if (_currentGear > 1 && currentGearRpm <= MathF.Max(downshiftRpm, idleRpm))
        {
            _currentGear -= 1;
        }
    }

    private void UpdateTyreScreech(float totalSpeed, float forwardSpeed, float lateralSpeed, float deltaTime)
    {
        if (_tyreEmitter is null)
        {
            return;
        }

        var slipRatio = lateralSpeed / MathF.Max(forwardSpeed, 1.0f);
        var speedGate = SafeInverseLerp(MathF.Max(minimumScreechSpeed * 0.5f, 0.0f), MathF.Max(minimumScreechSpeed, 0.01f), totalSpeed);
        var screechAmount = SafeInverseLerp(MathF.Max(tyreSlipThreshold, 0.0f), MathF.Max(tyreSlipFull, tyreSlipThreshold + 0.01f), slipRatio);
        screechAmount *= speedGate;

        UpdateTyreScreech(screechAmount, deltaTime);
    }

    private float GetTelemetryTyreScreechAmount(VehicleTelemetry telemetry)
    {
        var wheelSpinSpeed = MathF.Abs(telemetry.DrivenWheelSpeed);
        var vehicleSpeed = MathF.Abs(telemetry.ForwardSpeed);
        var spinningFasterThanMoving = wheelSpinSpeed > vehicleSpeed + MathF.Max(wheelSpinScreechThreshold, 0.0f);
        if (!telemetry.IsGrounded ||
            !spinningFasterThanMoving ||
            telemetry.TyreSmoke < MathF.Max(minimumTelemetryScreech, 0.0f))
        {
            return 0.0f;
        }

        return Math.Clamp(telemetry.TyreSmoke, 0.0f, 1.0f);
    }

    private void UpdateTyreScreech(float screechAmount, float deltaTime)
    {
        if (_tyreEmitter is null)
        {
            return;
        }

        var screechLerp = 1.0f - MathF.Exp(-MathF.Max(screechResponse, 0.0f) * deltaTime);
        _smoothedScreechAmount = Lerp(_smoothedScreechAmount, Math.Clamp(screechAmount, 0.0f, 1.0f), screechLerp);
        _tyreEmitter.Volume = _smoothedScreechAmount * MathF.Max(screechVolume, 0.0f);
        _tyreEmitter.Pitch = Lerp(Math.Clamp(screechPitchLow, 0.25f, 4.0f), Math.Clamp(screechPitchHigh, 0.25f, 4.0f), _smoothedScreechAmount);

        if (_smoothedScreechAmount > 0.02f)
        {
            if (!_tyreScreechActive)
            {
                _tyreEmitter.Stop();
                _tyreScreechActive = true;
            }

            EnsurePlaying(_tyreEmitter);
        }
        else
        {
            if (_tyreEmitter.Playing)
            {
                _tyreEmitter.Stop();
            }

            _tyreScreechActive = false;
        }
    }

    private void EnsurePlaying(SoundEmitterComponent emitter)
    {
        if (playOnCreate && !emitter.Playing && !string.IsNullOrWhiteSpace(emitter.Clip))
        {
            emitter.Play();
        }
    }

    private float ComputeWheelRpm(float signedDriveSpeed)
    {
        var circumference = 2.0f * MathF.PI * MathF.Max(wheelRadius, 0.01f);
        return signedDriveSpeed / circumference * 60.0f;
    }

    private float EstimateRpmForGear(float forwardSpeed, int gear)
    {
        var wheelRpm = MathF.Abs(ComputeWheelRpm(forwardSpeed));
        var gearRatio = MathF.Abs(GetGearRatio(gear));
        var rpm = wheelRpm * gearRatio * MathF.Max(finalDriveRatio, 0.01f);
        return Math.Clamp(rpm, MathF.Max(idleRpm, 0.0f), MathF.Max(redlineRpm, idleRpm + 100.0f));
    }

    private float ComputeTopSpeedForGear(int gear)
    {
        var ratio = MathF.Abs(GetGearRatio(gear)) * MathF.Max(finalDriveRatio, 0.01f);
        if (ratio <= 0.0001f)
        {
            return 0.0f;
        }

        var wheelCircumference = 2.0f * MathF.PI * MathF.Max(wheelRadius, 0.01f);
        var wheelRpmAtRedline = MathF.Max(redlineRpm, idleRpm + 100.0f) / ratio;
        return wheelRpmAtRedline * wheelCircumference / 60.0f;
    }

    private float GetCurrentGearRatio()
    {
        return GetGearRatio(_currentGear);
    }

    private float GetGearRatio(int gear)
    {
        return gear switch
        {
            -1 => MathF.Max(reverseGearRatio, 0.01f),
            1 => MathF.Max(firstGearRatio, 0.01f),
            2 => MathF.Max(secondGearRatio, 0.01f),
            3 => MathF.Max(thirdGearRatio, 0.01f),
            4 => MathF.Max(fourthGearRatio, 0.01f),
            5 => MathF.Max(fifthGearRatio, 0.01f),
            _ => MathF.Max(sixthGearRatio, 0.01f),
        };
    }

    private static float SafeInverseLerp(float min, float max, float value)
    {
        if (max <= min)
        {
            return 0.0f;
        }

        return Math.Clamp((value - min) / (max - min), 0.0f, 1.0f);
    }

    private static float Lerp(float a, float b, float t)
    {
        return a + (b - a) * Math.Clamp(t, 0.0f, 1.0f);
    }

    private static float EaseOut(float t)
    {
        var clamped = Math.Clamp(t, 0.0f, 1.0f);
        return 1.0f - (1.0f - clamped) * (1.0f - clamped);
    }
}
