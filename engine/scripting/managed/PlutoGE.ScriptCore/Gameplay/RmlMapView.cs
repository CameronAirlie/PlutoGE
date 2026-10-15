using System.Globalization;
using System.Numerics;
using System.Text;

namespace PlutoGE.ScriptCore.Gameplay;

/// <summary>
/// Draws a <see cref="MapLayout"/> image and pooled contact markers into one RML map frame.
/// Expected markup (ids are configurable through the prefix):
/// <code>
/// &lt;div id="minimap-frame" class="map-frame"&gt;          (square; RCSS: overflow: hidden; clip: always)
///   &lt;div id="minimap-layer"&gt;&lt;img id="minimap-image"/&gt;&lt;div id="minimap-contacts"/&gt;&lt;/div&gt;
/// &lt;/div&gt;
/// </code>
/// The layer is sized and offset in percentages of the frame, so RCSS may resize the frame
/// freely. With <see cref="VisibleWorldSpan"/> set, the layer pans so the focus point is centred
/// (a minimap); otherwise the whole layout fills the frame. Each marker gets the classes
/// "map-contact" plus the contact's Style; unused markers get "hidden". A heading is applied as
/// transform: rotate(..). Only changed styles are written, and the pool rebuilds itself after
/// native document recreation.
/// </summary>
public sealed class RmlMapView
{
    /// <summary>Marker content with a rotatable arrow image and a dot; style which one shows per contact class in RCSS.</summary>
    public static string ArrowMarker(string arrowImageSource) =>
        $"<img class=\"map-arrow\" src=\"{arrowImageSource.Replace("\"", "&quot;")}\"/><div class=\"map-dot\"></div>";

    private readonly RmlDocument _document;
    private readonly string _prefix;
    private readonly string _markerContent;
    private readonly List<MapContact> _sorted = [];
    private RmlElement[] _markers = [];
    // The style class each marker carries in the DOM. It is only reset when the pool is rebuilt:
    // forgetting it would leave a stale class (an enemy dot on a teammate) when a slot is reused.
    private string[] _applied = [];
    private bool?[] _hidden = [];
    private string[] _left = [], _top = [], _rotation = [], _opacity = [];
    private string _width = "", _height = "", _layerLeft = "", _layerTop = "", _image = "";
    private bool _built;

    /// <param name="markerContent">Inner RML for each pooled marker; image sources are document-relative.</param>
    public RmlMapView(RmlDocument document, string idPrefix, string markerContent, int poolSize = 32)
    {
        _document = document ?? throw new ArgumentNullException(nameof(document));
        if (string.IsNullOrWhiteSpace(idPrefix)) throw new ArgumentException("An id prefix is required.", nameof(idPrefix));
        if (poolSize is < 1 or > 512) throw new ArgumentOutOfRangeException(nameof(poolSize));
        _prefix = idPrefix;
        PoolSize = poolSize;
        _markerContent = markerContent;
    }

    public int PoolSize { get; }

    /// <summary>World metres shown across the frame around the focus point; null shows the whole layout.</summary>
    public float? VisibleWorldSpan { get; set; }

    /// <summary>Forgets cached positions and visibility, e.g. after the frame was hidden. Style classes stay tracked.</summary>
    public void Invalidate()
    {
        var count = _markers.Length;
        _hidden = new bool?[count]; _left = new string[count]; _top = new string[count];
        _rotation = new string[count]; _opacity = new string[count];
        _width = _height = _layerLeft = _layerTop = _image = "";
    }

    /// <summary>
    /// Renders the layout and contacts. Returns false while the native document is not live yet;
    /// call it again next frame.
    /// </summary>
    public bool Render(MapLayout layout, IReadOnlyList<MapContact> contacts, Vector3 focus)
    {
        if (!_built && !Build()) return false;
        var layer = _document.Element($"{_prefix}-layer");
        var layerPercent = VisibleWorldSpan is { } span ? MapMath.LayerSize(layout, 100, span) : 100;
        var offset = VisibleWorldSpan is null ? Vector2.Zero : MapMath.FocusOffset(layout.Normalize(focus), layerPercent, 100);
        var size = Percent(layerPercent);
        if (!Write(layer, "width", size, ref _width)) return Lost();
        Write(layer, "height", size, ref _height);
        Write(layer, "left", Percent(offset.X), ref _layerLeft);
        Write(layer, "top", Percent(offset.Y), ref _layerTop);
        var image = layout.ImageSourceFor(_document.Path);
        if (_image != image)
        {
            _document.Element($"{_prefix}-image")["src"] = image;
            _image = image;
        }

        _sorted.Clear();
        _sorted.AddRange(contacts);
        _sorted.Sort((a, b) => a.Priority.CompareTo(b.Priority));
        for (var i = 0; i < _markers.Length; i++)
        {
            var marker = _markers[i];
            var hidden = i >= _sorted.Count;
            if (_hidden[i] != hidden)
            {
                if (!marker.SetClass("hidden", hidden)) return Lost();
                _hidden[i] = hidden;
            }
            if (hidden) continue;
            var contact = _sorted[i];
            var style = contact.Style ?? "";
            if (_applied[i] != style)
            {
                if (_applied[i].Length > 0) marker.SetClass(_applied[i], false);
                if (style.Length > 0) marker.SetClass(style, true);
                _applied[i] = style;
            }
            var point = layout.Normalize(contact.Position) * 100;
            Write(marker, "left", Percent(point.X), ref _left[i]);
            Write(marker, "top", Percent(point.Y), ref _top[i]);
            Write(marker, "transform", contact.ShowsHeading
                ? $"rotate({contact.Heading.ToString("0", CultureInfo.InvariantCulture)}deg)" : "none", ref _rotation[i]);
            Write(marker, "opacity", Math.Clamp(contact.Opacity, 0, 1).ToString("0.##", CultureInfo.InvariantCulture), ref _opacity[i]);
        }
        return true;
    }

    private bool Build()
    {
        var markup = new StringBuilder();
        for (var i = 0; i < PoolSize; i++)
            markup.Append($"<div id=\"{_prefix}-c{i}\" class=\"map-contact hidden\">{_markerContent}</div>");
        _document.Element($"{_prefix}-contacts").Markup = markup.ToString();
        _markers = new RmlElement[PoolSize];
        for (var i = 0; i < PoolSize; i++) _markers[i] = _document.Element($"{_prefix}-c{i}");
        _applied = Enumerable.Repeat("", PoolSize).ToArray(); // Fresh markup carries no style class.
        Invalidate();
        _built = _markers[0].SetClass("hidden", true);
        return _built;
    }

    private bool Lost()
    {
        _built = false;
        return false;
    }

    private static string Percent(float value) => value.ToString("0.##", CultureInfo.InvariantCulture) + "%";

    private static bool Write(RmlElement element, string property, string value, ref string cache)
    {
        if (cache == value) return true;
        if (!element.SetStyle(property, value)) return false;
        cache = value;
        return true;
    }
}
