using System.Globalization;
using System.Numerics;
using System.Text.Json;

namespace PlutoGE.ScriptCore.Gameplay;

/// <summary>
/// The world square covered by a baked map image (a <c>.plutomap</c> file written by the editor's
/// Bake Map command or <c>PlutoGEBakeMap</c>). Maps are north-up: world -Z is the top edge and +X
/// the right edge. Normalized map space is (0,0) at the north-west corner, (1,1) at the south-east.
/// </summary>
public sealed record MapLayout(string Name, string Image, float CentreX, float CentreZ, float WorldSize)
{
    public const string Extension = ".plutomap";

    public Vector2 Normalize(Vector3 world) =>
        new((world.X - CentreX) / WorldSize + .5f, (world.Z - CentreZ) / WorldSize + .5f);

    public Vector3 ToWorld(Vector2 normalized, float height = 0) =>
        new((normalized.X - .5f) * WorldSize + CentreX, height, (normalized.Y - .5f) * WorldSize + CentreZ);

    /// <summary>
    /// RML <c>img src</c> for <see cref="Image"/> relative to the document that shows it, since RmlUi
    /// resolves image sources beside their document. For example "UI/hud.rml" and image
    /// "Maps/Yard.tga" give "../Maps/Yard.tga". Accepts asset-relative or project:// document paths.
    /// </summary>
    public string ImageSourceFor(string documentPath)
    {
        static string[] Parts(string path)
        {
            if (path.StartsWith("project://", StringComparison.Ordinal)) path = path["project://".Length..];
            return path.Replace('\\', '/').Split('/', StringSplitOptions.RemoveEmptyEntries);
        }
        var document = Parts(documentPath);
        var folder = document[..^Math.Min(1, document.Length)];
        var image = Parts(Image);
        var shared = 0;
        while (shared < folder.Length && shared < image.Length - 1 &&
               string.Equals(folder[shared], image[shared], StringComparison.OrdinalIgnoreCase)) shared++;
        return string.Join('/', Enumerable.Repeat("..", folder.Length - shared).Concat(image[shared..]));
    }

    /// <summary>Loads a layout asset, e.g. <c>project://Maps/Shipment.plutomap</c>; null when missing or invalid.</summary>
    public static MapLayout? Load(string assetReference) =>
        Native.ScriptBridge.TryReadTextAsset(assetReference, out var json) ? Parse(json) : null;

    /// <summary>Loads <c>project://{folder}/{scene}.plutomap</c>, the default bake location for a scene.</summary>
    public static MapLayout? ForScene(string scene, string folder = "Maps") =>
        string.IsNullOrWhiteSpace(scene) ? null : Load($"project://{folder.Trim('/')}/{scene}{Extension}");

    /// <summary>Parses layout JSON. Returns null unless the size is positive and every value is finite.</summary>
    public static MapLayout? Parse(string json)
    {
        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;
            static float Number(JsonElement root, string name) =>
                root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Number ? value.GetSingle() : float.NaN;
            static string Text(JsonElement root, string name) =>
                root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String ? value.GetString() ?? "" : "";
            var image = Text(root, "Image");
            if (image.StartsWith("project://", StringComparison.Ordinal)) image = image["project://".Length..];
            var layout = new MapLayout(Text(root, "Name"), image, Number(root, "CentreX"), Number(root, "CentreZ"), Number(root, "WorldSize"));
            return float.IsFinite(layout.CentreX) && float.IsFinite(layout.CentreZ) && float.IsFinite(layout.WorldSize) &&
                layout.WorldSize > 0 && layout.Image.Length > 0 ? layout : null;
        }
        catch (JsonException)
        {
            return null;
        }
    }

    public string ToJson() => string.Create(CultureInfo.InvariantCulture,
        $"{{\n  \"Name\": {JsonSerializer.Serialize(Name)},\n  \"Image\": {JsonSerializer.Serialize("project://" + Image)},\n  \"CentreX\": {CentreX},\n  \"CentreZ\": {CentreZ},\n  \"WorldSize\": {WorldSize},\n  \"North\": \"-Z\"\n}}\n");
}

/// <summary>
/// One map marker. <see cref="Style"/> is the RCSS class applied to the marker (for example
/// "self", "friendly", "enemy"). Heading is clockwise degrees from north, or NaN to draw the
/// marker unrotated (unknown facing). Lower <see cref="Priority"/> values keep their pooled
/// marker first when there are more contacts than markers.
/// </summary>
public readonly record struct MapContact(int Id, string Style, Vector3 Position, float Heading = float.NaN, float Opacity = 1, int Priority = 0)
{
    public bool ShowsHeading => float.IsFinite(Heading);
}

public static class MapMath
{
    /// <summary>Clockwise degrees from north (-Z) in [0, 360), or NaN for an invalid or vertical vector.</summary>
    public static float Heading(Vector3 forward)
    {
        if (!float.IsFinite(forward.X) || !float.IsFinite(forward.Z) || forward.X * forward.X + forward.Z * forward.Z < 1e-8f)
            return float.NaN;
        var degrees = MathF.Atan2(forward.X, -forward.Z) * 180 / MathF.PI;
        return degrees < 0 ? degrees + 360 : degrees;
    }

    /// <summary>Layer edge length, in frame units, for a frame that shows visibleWorld metres across.</summary>
    public static float LayerSize(MapLayout layout, float frameSize, float visibleWorld) =>
        frameSize * layout.WorldSize / MathF.Max(.01f, visibleWorld);

    /// <summary>Top-left of a layer that places the normalized focus point at the frame centre.</summary>
    public static Vector2 FocusOffset(Vector2 focus, float layerSize, float frameSize) =>
        new(frameSize * .5f - focus.X * layerSize, frameSize * .5f - focus.Y * layerSize);
}
