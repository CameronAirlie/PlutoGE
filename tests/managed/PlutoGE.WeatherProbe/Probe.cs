using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.WeatherProbe;

// Drives a cloud volume the way a weather script would; the C++ test checks the native component afterwards.
public sealed class WeatherProbe : ScriptBehaviour
{
    [SerializedField] public bool Found;
    [SerializedField] public bool RoundTrips;
    [SerializedField] public float InitialCoverage = -1.0f;
    [SerializedField] public string Failure = string.Empty;

    public override void OnCreate()
    {
        try
        {
            var deck = GameObject.Find("Clouds") ?? throw new InvalidOperationException("No cloud entity");
            var clouds = deck.GetComponent<VolumetricCloudComponent>() ??
                         throw new InvalidOperationException("No VolumetricCloudComponent");
            Found = clouds.Exists;
            InitialCoverage = clouds.Coverage;

            // Close the deck into overcast and turn the wind.
            clouds.Coverage = 0.95f;
            clouds.Density = 0.8f;
            clouds.WindDirection = new Vector3(0.0f, 0.0f, 1.0f);
            clouds.WindSpeed = 14.0f;
            clouds.CastShadows = false;
            clouds.ShadowDistance = 5000.0f;
            clouds.ShadowDensity = 0.5f;
            clouds.Coverage = 0.95f;

            RoundTrips = Math.Abs(clouds.Coverage - 0.95f) < 1e-4f && Math.Abs(clouds.Density - 0.8f) < 1e-4f &&
                         clouds.WindDirection == new Vector3(0.0f, 0.0f, 1.0f) && !clouds.CastShadows &&
                         Math.Abs(clouds.ShadowDistance - 5000.0f) < 1e-2f;

            // Native clamps apply through the bridge.
            clouds.Coverage = 3.0f;
            if (Math.Abs(clouds.Coverage - 1.0f) > 1e-4f)
                throw new InvalidOperationException($"Coverage was not clamped: {clouds.Coverage}");
        }
        catch (Exception exception)
        {
            Failure = exception.ToString();
        }
    }
}
