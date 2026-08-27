using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Reusable tuning data for a particular raycast-vehicle model. Create assets
/// from the Content Browser's Create > Scriptable Object menu, then assign one
/// to any RaycastVehicleController.
/// </summary>
public sealed class RaycastVehicleSettings : ScriptableObject
{
    [SerializedField] public float Mass = 1250.0f;
    [SerializedField] public float WheelRadius = 0.5f;
    [SerializedField] public float DrivenWheelInertia = 1.5f;

    [SerializedField] public float SuspensionTravel = 0.32f;
    [SerializedField] public float RideHeight = 0.42f;
    [SerializedField] public float SpringStrength = 24000.0f;
    [SerializedField] public float DamperStrength = 3000.0f;

    [SerializedField] public float Acceleration = 40.0f;
    [SerializedField] public float ThrottleResponse = 4.0f;
    [SerializedField] public float ReverseAcceleration = 14.0f;
    [SerializedField] public float BrakePower = 15.0f;
    [SerializedField] public float MaxSpeed = 92.0f;
    [SerializedField] public int Drivetrain = 0;

    [SerializedField] public float FrontGrip = 6.0f;
    [SerializedField] public float RearGrip = 5.0f;
    [SerializedField] public float GripLimit = 1.5f;
    [SerializedField] public float DriveGrip = 1.0f;
    [SerializedField] public float PeakLongitudinalSlip = 0.12f;
    [SerializedField] public float FullLongitudinalSlip = 1.0f;
    [SerializedField] public float SpinningTyreLongitudinalGrip = 0.55f;
    [SerializedField] public float SpinningTyreLateralGrip = 0.15f;
    [SerializedField] public float BrakeGrip = 1.0f;
    [SerializedField] public float HandbrakeGrip = 0.45f;

    [SerializedField] public float AirDensity = 1.225f;
    [SerializedField] public float DownforceCoefficient = 1.2f;
    [SerializedField] public float DownforceArea = 2.2f;
    [SerializedField] public float FrontDownforceBalance = 0.45f;

    [SerializedField] public float IdleRpm = 900.0f;
    [SerializedField] public float RedlineRpm = 7200.0f;
    [SerializedField] public float FinalDriveRatio = 4.5f;
    [SerializedField] public float ReverseGearRatio = 3.2f;
    [SerializedField] public float FirstGearRatio = 4.0f;
    [SerializedField] public float SecondGearRatio = 2.0f;
    [SerializedField] public float ThirdGearRatio = 1.0f;
    [SerializedField] public float FourthGearRatio = 0.5f;
    [SerializedField] public float FifthGearRatio = 0.3f;
    [SerializedField] public float SixthGearRatio = 0.25f;
    [SerializedField] public float UpshiftRpm = 6800.0f;
    [SerializedField] public float DownshiftRpm = 1200.0f;
    [SerializedField] public float ShiftHysteresisRpm = 250.0f;
    [SerializedField] public float EngineResponse = 20.0f;
    [SerializedField] public float LaunchRpm = 3400.0f;
    [SerializedField] public float PeakTorqueRpm = 4800.0f;
    [SerializedField] public float ShiftDuration = 0.5f;
    [SerializedField] public bool ManualTransmission = false;

    [SerializedField] public float MaxSteerAngle = 32.0f;
    [SerializedField] public float SteerSharpness = 5.0f;
    [SerializedField] public float HighSpeedSteerFade = 0.12f;
}
