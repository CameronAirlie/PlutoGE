namespace PlutoGE.Editor.Avalonia;

internal readonly record struct ValidationSize(int Width, int Height);
internal readonly record struct CadenceResult(double AverageHz, double MaximumErrorPercent, bool Passed);

internal static class ViewportValidationMath
{
    internal static IReadOnlyList<ValidationSize> ResizeSequence { get; } =
    [
        new(1280, 720),
        new(1500, 900),
        new(1024, 768),
        new(1720, 960),
    ];

    internal static float AdvanceYaw(float yawDegrees, float degreesPerSecond, float deltaSeconds)
    {
        var yaw = yawDegrees + degreesPerSecond * Math.Clamp(deltaSeconds, 0.0f, 0.25f);
        return MathF.IEEERemainder(yaw, 360.0f);
    }

    internal static CadenceResult AnalyzeCadence(IEnumerable<double> frameSeconds, double targetHz, double tolerancePercent = 5.0)
    {
        var samples = frameSeconds.Where(value => value > 0.0 && double.IsFinite(value)).ToArray();
        if (samples.Length == 0 || targetHz <= 0.0) return new(0.0, double.PositiveInfinity, false);
        var averageSeconds = samples.Average();
        var averageHz = 1.0 / averageSeconds;
        var targetSeconds = 1.0 / targetHz;
        var maximumError = samples.Max(value => Math.Abs(value - targetSeconds) / targetSeconds * 100.0);
        return new(averageHz, maximumError, maximumError <= tolerancePercent);
    }
}
