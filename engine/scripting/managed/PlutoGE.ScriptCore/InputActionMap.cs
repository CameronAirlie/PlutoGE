using System.Text.Json;
using System.Text.Json.Serialization;

namespace PlutoGE.ScriptCore;

public enum InputBindingKind { Key, GamepadButton, GamepadAxis, MouseButton, MouseAxis }

public enum MouseAxis { X, Y }

public sealed class InputBinding
{
    public InputBindingKind Kind { get; set; }
    public KeyCode Key { get; set; }
    public GamepadButton Button { get; set; }
    public GamepadAxis Axis { get; set; }
    public MouseButton MouseButton { get; set; }
    public MouseAxis MouseAxis { get; set; }
    public int Gamepad { get; set; }
    public float Scale { get; set; } = 1.0f;
    public float DeadZone { get; set; } = 0.15f;
    /// <summary>How the control becomes action activity (tap, hold, multi-tap...); null is a plain press.</summary>
    public InputTrigger? Trigger { get; set; }
}

public sealed class InputActionDefinition
{
    public string Name { get; set; } = string.Empty;
    public List<InputBinding> Bindings { get; set; } = [];
}

/// <summary>
/// A Unity-style asset that maps named actions to keyboard, mouse, and controller bindings. Bindings
/// with a stateful <see cref="InputBinding.Trigger"/> (tap, hold, multi-tap) need <see cref="Update"/>
/// once per frame from the map's owner; plain press bindings are read directly and work without it.
/// </summary>
public sealed class InputActionMap
{
    public List<InputActionDefinition> Actions { get; set; } = [];
    [JsonIgnore] private Dictionary<string, InputActionDefinition>? _lookup;
    // Trigger state per binding object, so editing or replacing a binding at runtime starts it afresh.
    [JsonIgnore] private readonly Dictionary<InputBinding, TriggerState> _triggers = new(ReferenceEqualityComparer.Instance);
    [JsonIgnore] private int _updates;
    [JsonIgnore] private bool _warnedNotUpdated;

    private sealed class TriggerState(InputTrigger trigger)
    {
        public InputTrigger Trigger { get; } = trigger;
        public InputTriggerKind Kind { get; } = trigger.Kind;
        public IInputTriggerEvaluator Evaluator { get; } = InputTriggers.Create(trigger);
        public InputTriggerResult Result { get; set; }
        public float Value { get; set; }
        public bool PreviousDown { get; set; }
        public int LastUpdate { get; set; }
    }

    private static readonly JsonSerializerOptions ReadOptions = CreateReadOptions();

    private static JsonSerializerOptions CreateReadOptions()
    {
        var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
        options.Converters.Add(new JsonStringEnumConverter());
        return options;
    }

    /// <summary>Parses a mapping in the <c>.plutoinput</c> JSON format.</summary>
    public static InputActionMap FromJson(string json)
    {
        var map = JsonSerializer.Deserialize<InputActionMap>(json, ReadOptions)
                  ?? throw new InvalidDataException("Input mapping JSON is empty.");
        map.RebuildLookup();
        return map;
    }

    public static InputActionMap Load(string assetReference)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(assetReference);
        var relative = assetReference.StartsWith("project://", StringComparison.OrdinalIgnoreCase)
            ? assetReference[10..] : assetReference;
        var options = ReadOptions;
        InputActionMap? loaded;
        if (Native.ScriptBridge.TryReadTextAsset(assetReference.StartsWith("asset://", StringComparison.Ordinal)
                || assetReference.StartsWith("project://", StringComparison.OrdinalIgnoreCase)
                || assetReference.StartsWith("engine://", StringComparison.OrdinalIgnoreCase)
                ? assetReference : "project://" + relative, out var text))
            loaded = JsonSerializer.Deserialize<InputActionMap>(text, options);
        else if (assetReference.StartsWith("asset://", StringComparison.Ordinal))
            throw new FileNotFoundException("The input mapping logical asset is unavailable.", assetReference);
        else
            loaded = ProjectStorage.ReadAssetJson<InputActionMap>(relative, options);
        var map = loaded ?? throw new InvalidDataException($"Input mapping asset '{assetReference}' is empty.");
        map.RebuildLookup();
        return map;
    }

    public void Save(string assetReference)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(assetReference);
        var relative = assetReference.StartsWith("project://", StringComparison.OrdinalIgnoreCase)
            ? assetReference[10..] : assetReference;
        var options = new JsonSerializerOptions { WriteIndented = true };
        options.Converters.Add(new JsonStringEnumConverter());
        ProjectStorage.WriteAssetJson(relative, this, options);
    }

    public static InputActionMap CreateVehicleDefaults() => new()
    {
        Actions =
        [
            new() { Name = "Throttle", Bindings = [new() { Kind = InputBindingKind.Key, Key = KeyCode.W }, new() { Kind = InputBindingKind.GamepadAxis, Axis = GamepadAxis.RightTrigger }] },
            new() { Name = "Brake", Bindings = [new() { Kind = InputBindingKind.Key, Key = KeyCode.S }, new() { Kind = InputBindingKind.GamepadAxis, Axis = GamepadAxis.LeftTrigger }] },
            new() { Name = "Steer", Bindings = [new() { Kind = InputBindingKind.Key, Key = KeyCode.A }, new() { Kind = InputBindingKind.Key, Key = KeyCode.D, Scale = -1.0f }, new() { Kind = InputBindingKind.GamepadAxis, Axis = GamepadAxis.LeftX, Scale = -1.0f }] },
            new() { Name = "CameraOrbitHorizontal", Bindings = [new() { Kind = InputBindingKind.GamepadAxis, Axis = GamepadAxis.RightX, DeadZone = 0.15f }] },
            new() { Name = "CameraOrbitVertical", Bindings = [new() { Kind = InputBindingKind.GamepadAxis, Axis = GamepadAxis.RightY, Scale = -1.0f, DeadZone = 0.15f }] },
            new() { Name = "Handbrake", Bindings = [new() { Kind = InputBindingKind.Key, Key = KeyCode.Space }, new() { Kind = InputBindingKind.GamepadButton, Button = GamepadButton.A }] },
            new() { Name = "ShiftUp", Bindings = [new() { Kind = InputBindingKind.Key, Key = KeyCode.E }, new() { Kind = InputBindingKind.GamepadButton, Button = GamepadButton.RightBumper }] },
            new() { Name = "ShiftDown", Bindings = [new() { Kind = InputBindingKind.Key, Key = KeyCode.Q }, new() { Kind = InputBindingKind.GamepadButton, Button = GamepadButton.LeftBumper }] },
            new() { Name = "Recover", Bindings = [new() { Kind = InputBindingKind.Key, Key = KeyCode.R }, new() { Kind = InputBindingKind.GamepadButton, Button = GamepadButton.Y }] }
        ]
    };

    /// <summary>
    /// Advances stateful binding triggers by one frame. Call once per frame from the map's owner, before
    /// reading actions, with the frame's delta in seconds (unscaled if gameplay time is scaled).
    /// </summary>
    public void Update(float deltaTime)
    {
        _updates++;
        foreach (var action in Actions)
            foreach (var binding in action.Bindings)
            {
                if (binding.Trigger is not { IsStateful: true } trigger) continue;
                if (!_triggers.TryGetValue(binding, out var state) || !ReferenceEquals(state.Trigger, trigger) || state.Kind != trigger.Kind)
                    _triggers[binding] = state = new TriggerState(trigger);
                var value = Sample(binding);
                var down = MathF.Abs(value) >= 0.5f;
                var control = new InputControlSample(down, WentDown(binding, down, state.PreviousDown),
                                                     WentUp(binding, down, state.PreviousDown));
                state.Result = state.Evaluator.Update(control, deltaTime);
                state.Value = value;
                state.PreviousDown = down;
                state.LastUpdate = _updates;
            }
        // Forget bindings removed since the last update.
        if (_triggers.Count > 0)
            foreach (var stale in _triggers.Where(entry => entry.Value.LastUpdate != _updates).Select(entry => entry.Key).ToList())
                _triggers.Remove(stale);
    }

    /// <summary>Clears trigger progress (a half-held hold, a pending tap), for example while gameplay is paused.</summary>
    public void Reset() => _triggers.Clear();

    public float GetAxis(string action)
    {
        var definition = Find(action);
        if (definition is null) return 0.0f;
        float value = 0.0f;
        foreach (var binding in definition.Bindings)
        {
            if (binding.Trigger is { IsStateful: true })
            {
                // A stateful trigger gates its control: it contributes only while the trigger is active.
                if (TriggerResult(binding, out var result, out var sampled) && result.Active)
                    value += (IsAnalog(binding) ? sampled : 1.0f) * binding.Scale;
                continue;
            }
            value += Sample(binding) * binding.Scale;
        }
        return Math.Clamp(value, -1.0f, 1.0f);
    }

    private static bool IsAnalog(InputBinding binding) => binding.Kind is InputBindingKind.GamepadAxis or InputBindingKind.MouseAxis;

    /// <summary>The binding's raw value with its dead zone applied (keys and buttons are 0 or 1); 0 when disabled.</summary>
    private static float Sample(InputBinding binding)
    {
        if (binding.Scale == 0.0f) return 0.0f;
        var sample = binding.Kind switch
        {
            InputBindingKind.Key => Input.IsKeyDown(binding.Key) ? 1.0f : 0.0f,
            InputBindingKind.GamepadButton => Input.IsGamepadButtonDown(binding.Button, binding.Gamepad) ? 1.0f : 0.0f,
            InputBindingKind.GamepadAxis => Input.GetGamepadAxis(binding.Axis, binding.Gamepad),
            InputBindingKind.MouseButton => Input.IsMouseButtonDown(binding.MouseButton) ? 1.0f : 0.0f,
            InputBindingKind.MouseAxis => binding.MouseAxis == MouseAxis.X ? Input.MouseDelta.X : Input.MouseDelta.Y,
            _ => 0.0f
        };
        // The platform layer already exposes triggers in the conventional
        // 0..1 range. Normalizing them here a second time turns a released
        // trigger into 0.5, which makes trigger-bound actions stay active.
        return MathF.Abs(sample) < Math.Clamp(binding.DeadZone, 0.0f, 0.99f) ? 0.0f : sample;
    }

    // Native edges catch a press and release inside one frame; analog controls compare with the previous update.
    private static bool WentDown(InputBinding binding, bool down, bool previousDown) => binding.Scale != 0.0f && binding.Kind switch
    {
        InputBindingKind.Key => Input.IsKeyPressed(binding.Key),
        InputBindingKind.GamepadButton => Input.IsGamepadButtonPressed(binding.Button, binding.Gamepad),
        InputBindingKind.MouseButton => Input.IsMouseButtonPressed(binding.MouseButton),
        _ => down && !previousDown
    };

    private static bool WentUp(InputBinding binding, bool down, bool previousDown) => binding.Kind switch
    {
        InputBindingKind.Key => Input.IsKeyReleased(binding.Key),
        InputBindingKind.MouseButton => Input.IsMouseButtonReleased(binding.MouseButton),
        _ => previousDown && !down
    };

    private bool TriggerResult(InputBinding binding, out InputTriggerResult result, out float value)
    {
        if (_triggers.TryGetValue(binding, out var state) && ReferenceEquals(state.Trigger, binding.Trigger) &&
            state.Kind == binding.Trigger!.Kind)
        {
            result = state.Result;
            value = state.Value;
            return true;
        }
        result = default;
        value = 0.0f;
        if (_updates == 0 && !_warnedNotUpdated)
        {
            _warnedNotUpdated = true;
            Debug.LogWarning("InputActionMap: a binding uses a tap/hold/multi-tap trigger but the map is never updated. " +
                             "Call Update(deltaTime) once per frame; until then those bindings stay idle.");
        }
        return false;
    }

    public bool IsDown(string action) => MathF.Abs(GetAxis(action)) > 0.5f;
    /// <summary>
    /// True on the frame the action triggers: a button press or a gamepad axis becoming nonzero outside
    /// its dead zone, or, for a binding with a stateful trigger, the frame that trigger fires (a tap's
    /// release, a hold reaching its duration, the last press of a multi-tap). Holding does not repeat;
    /// reads do not consume presses.
    /// </summary>
    public bool WasPressed(string action)
    {
        var definition = Find(action);
        return definition is not null && definition.Bindings.Any(binding => binding.Trigger is { IsStateful: true }
            ? TriggerResult(binding, out var result, out _) && result.Triggered
            : binding.Kind switch
        {
            InputBindingKind.Key => Input.IsKeyPressed(binding.Key),
            InputBindingKind.GamepadButton => Input.IsGamepadButtonPressed(binding.Button, binding.Gamepad),
            InputBindingKind.MouseButton => Input.IsMouseButtonPressed(binding.MouseButton),
            InputBindingKind.GamepadAxis => AxisWasPressed(binding),
            _ => false
        });
    }

    // Compare frame snapshots, not query history: multiple consumers get the same
    // edge, and actions do not need to be polled while menus/gameplay are inactive.
    private static bool AxisWasPressed(InputBinding binding)
    {
        if (binding.Scale == 0.0f) return false;
        float deadZone = Math.Clamp(binding.DeadZone, 0.0f, 0.99f);
        static bool Active(float value, float threshold) => value != 0.0f && MathF.Abs(value) >= threshold;
        return Active(Input.GetGamepadAxis(binding.Axis, binding.Gamepad), deadZone) &&
            !Active(Native.ScriptBridge.GetPreviousGamepadAxis(binding.Gamepad, (int)binding.Axis), deadZone);
    }

    private InputActionDefinition? Find(string action)
    {
        if (_lookup is null) RebuildLookup();
        return _lookup!.GetValueOrDefault(action);
    }
    private void RebuildLookup() => _lookup = Actions
        .Where(action => !string.IsNullOrWhiteSpace(action.Name))
        .GroupBy(action => action.Name, StringComparer.OrdinalIgnoreCase)
        .ToDictionary(group => group.Key, group => group.Last(), StringComparer.OrdinalIgnoreCase);
}
