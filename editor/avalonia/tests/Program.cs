using PlutoGE.Editor.Avalonia;
using System.Numerics;

static void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

var yaw = 0.0f;
for (var frame = 0; frame < 144 * 60 * 10; ++frame)
    yaw = ViewportValidationMath.AdvanceYaw(yaw, 35.0f, 1.0f / 144.0f);
Require(float.IsFinite(yaw) && yaw is >= -180.0f and <= 180.0f, "Continuous camera rotation became unstable.");

foreach (var targetHz in new[] { 60.0, 120.0, 144.0 })
{
    var samples = Enumerable.Repeat(1.0 / targetHz, (int)targetHz * 10);
    var result = ViewportValidationMath.AnalyzeCadence(samples, targetHz);
    Require(result.Passed, $"{targetHz} Hz cadence validation failed.");
    Require(Math.Abs(result.AverageHz - targetHz) < 0.001, $"{targetHz} Hz average was inaccurate.");
}

Require(ViewportValidationMath.ResizeSequence.All(size => size.Width > 0 && size.Height > 0), "Resize sequence contains an invalid extent.");
Require(ViewportValidationMath.ResizeSequence.Distinct().Count() == ViewportValidationMath.ResizeSequence.Count, "Resize sequence does not exercise unique extents.");

var look = ViewportCameraController.AdvanceLook(0.0f, 0.0f, 100.0f, -50.0f);
Require(look.Yaw < 0.0f && look.Pitch > 0.0f, "Right-drag mouse look did not update yaw and pitch.");
var movedCamera = ViewportCameraController.AdvancePosition(
    new Vector3(0.0f, 2.0f, 6.0f), look.Yaw, look.Pitch, Vector3.UnitZ, 6.0f, 1.0f);
Require(Vector3.Distance(movedCamera, new Vector3(0.0f, 2.0f, 6.0f)) > 5.9f,
    "Focused WASD camera movement did not advance the editor camera.");

var pivot = new Vector3(2.0f, 1.0f, -3.0f);
var orbitDistance = 8.0f;
var orbitPosition = ViewportCameraController.OrbitPosition(pivot, look.Yaw, look.Pitch, orbitDistance);
Require(Math.Abs(Vector3.Distance(orbitPosition, pivot) - orbitDistance) < 0.001f,
    "Alt-left orbit did not preserve the scene-view pivot distance.");
var panned = ViewportCameraController.Pan(orbitPosition, pivot, look.Yaw, look.Pitch, 40.0f, -15.0f, 0.01f);
Require(Vector3.Distance(panned.Position - orbitPosition, panned.Pivot - pivot) < 0.001f,
    "Middle-drag pan did not move the camera and pivot together.");
Require(ViewportCameraController.DollyDistance(orbitDistance, 1.0f) < orbitDistance,
    "Forward wheel input did not zoom toward the scene-view pivot.");
Require(ViewportCameraController.FrameDistance(2.0f) > ViewportCameraController.FrameDistance(0.25f),
    "Selection framing did not account for transform scale.");

var windowOneYaw = ViewportValidationMath.AdvanceYaw(10.0f, 20.0f, 1.0f);
var windowTwoYaw = ViewportValidationMath.AdvanceYaw(-30.0f, -10.0f, 1.0f);
Require(windowOneYaw != windowTwoYaw, "Independent viewport state was aliased across windows.");

Console.WriteLine("Viewport validation passed: Unity-style orbit/pan/fly/zoom/framing, continuous rotation, resize sequence, 60/120/144 Hz cadence, and independent window state.");
