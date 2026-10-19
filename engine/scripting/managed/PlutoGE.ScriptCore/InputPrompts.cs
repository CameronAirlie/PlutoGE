using System.Net;
using System.Text.RegularExpressions;
using System.Xml.Linq;

namespace PlutoGE.ScriptCore;

/// <summary>Device-specific, asset-driven labels; contains no project action names.</summary>
public static class InputPrompts
{
    private static readonly Regex Token = new(@"\{([A-Za-z0-9_.-]+)\}", RegexOptions.Compiled);
    public static string Format(InputActionMap map, string template, InputDevice? device = null) =>
        Token.Replace(template, match => Binding(map, match.Groups[1].Value, device));

    public static string Binding(InputActionMap map, string action, InputDevice? device = null)
    {
        ArgumentNullException.ThrowIfNull(map);
        var selected = device ?? Input.ActiveDevice;
        var definition = map.Actions.LastOrDefault(a => string.Equals(a.Name, action, StringComparison.OrdinalIgnoreCase));
        var labels = definition?.Bindings.Where(b => float.IsFinite(b.Scale) && b.Scale != 0 &&
            (selected == InputDevice.Controller
                ? b.Kind is InputBindingKind.GamepadButton or InputBindingKind.GamepadAxis
                : b.Kind is InputBindingKind.Key or InputBindingKind.MouseButton or InputBindingKind.MouseAxis))
            .Select(binding => Decorate(Label(binding), binding.Trigger)).Where(label => label.Length > 0).Distinct().ToArray() ?? [];
        return labels.Length == 0 ? "UNBOUND" : string.Join(" / ", labels);
    }

    // Triggers that change how the control is used are spelled out; a tap or single press reads as the control itself.
    private static string Decorate(string label, InputTrigger? trigger) => label.Length == 0 || trigger is null ? label : trigger.Kind switch
    {
        InputTriggerKind.Hold => "HOLD " + label,
        InputTriggerKind.MultiTap when trigger.Count > 1 => label + " x" + trigger.Count,
        _ => label
    };

    private static string Label(InputBinding binding) => binding.Kind switch
    {
        InputBindingKind.Key when binding.Key != KeyCode.Unknown && Enum.IsDefined(binding.Key) =>
            binding.Key is >= KeyCode.D0 and <= KeyCode.D9 ? ((int)binding.Key - (int)KeyCode.D0).ToString() :
            Regex.Replace(binding.Key.ToString(), "([a-z])([A-Z])", "$1 $2").ToUpperInvariant(),
        InputBindingKind.MouseButton when Enum.IsDefined(binding.MouseButton) => binding.MouseButton switch
        { MouseButton.Left => "LMB", MouseButton.Right => "RMB", MouseButton.Middle => "MMB", _ => "MOUSE " + ((int)binding.MouseButton + 1) },
        InputBindingKind.MouseAxis when Enum.IsDefined(binding.MouseAxis) => "MOUSE " + binding.MouseAxis.ToString().ToUpperInvariant(),
        InputBindingKind.GamepadButton when Enum.IsDefined(binding.Button) => binding.Button switch
        {
            GamepadButton.LeftBumper => "LB", GamepadButton.RightBumper => "RB",
            GamepadButton.LeftStick => "LS CLICK", GamepadButton.RightStick => "RS CLICK",
            GamepadButton.DpadUp => "D-PAD UP", GamepadButton.DpadRight => "D-PAD RIGHT",
            GamepadButton.DpadDown => "D-PAD DOWN", GamepadButton.DpadLeft => "D-PAD LEFT",
            _ => binding.Button.ToString().ToUpperInvariant()
        },
        InputBindingKind.GamepadAxis when Enum.IsDefined(binding.Axis) => binding.Axis switch
        {
            GamepadAxis.LeftX => "LS LEFT/RIGHT", GamepadAxis.LeftY => "LS UP/DOWN",
            GamepadAxis.RightX => "RS LEFT/RIGHT", GamepadAxis.RightY => "RS UP/DOWN",
            GamepadAxis.LeftTrigger => "LT", GamepadAxis.RightTrigger => "RT", _ => ""
        },
        _ => ""
    };
}

/// <summary>Refresh leaf elements marked with id and data-input-prompt in an RML asset.</summary>
public sealed class RmlInputPrompts
{
    private readonly RmlDocument _document;
    private readonly Func<InputActionMap> _map;
    private readonly List<(string Id, string Template)> _bindings = [];
    public RmlInputPrompts(RmlDocument document, Func<InputActionMap> map)
    { _document = document; _map = map; }

    public void Bind(string id, string template) => _bindings.Add((id, template));
    public void LoadDocumentBindings()
    {
        var reference = _document.Path.Contains("://", StringComparison.Ordinal) ? _document.Path : "project://" + _document.Path;
        var source = Native.ScriptBridge.TryReadTextAsset(reference, out var text) ? text :
            ProjectStorage.ReadAssetText(reference.StartsWith("project://", StringComparison.OrdinalIgnoreCase) ? reference[10..] : reference);
        var root = XDocument.Parse(source);
        _bindings.Clear();
        foreach (var element in root.Descendants())
            if (element.Attribute("data-input-prompt") is { } prompt && element.Attribute("id") is { } id)
                Bind(id.Value, prompt.Value);
    }
    public void Refresh()
    {
        var map = _map();
        var device = Input.ActiveDevice;
        foreach (var binding in _bindings)
        {
            var element = _document.Element(binding.Id);
            // Read the live template to respect document hot reload; the source template
            // also lets the first refresh succeed before the native DOM has appeared.
            var template = element["data-input-prompt"];
            if (string.IsNullOrEmpty(template)) template = binding.Template;
            var markup = WebUtility.HtmlEncode(InputPrompts.Format(map, template, device));
            // Native marker detects recreated documents and avoids stale managed caches.
            if (element["data-pluto-input-prompt"] == markup) continue;
            if (!element.SetClass("input-prompt", true)) continue;
            element.Markup = markup;
            element["data-pluto-input-prompt"] = markup;
        }
    }
}
