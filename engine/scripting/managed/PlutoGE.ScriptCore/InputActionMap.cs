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
}

public sealed class InputActionDefinition
{
    public string Name { get; set; } = string.Empty;
    public List<InputBinding> Bindings { get; set; } = [];
}

/// <summary>A Unity-style asset that maps named actions to keyboard, mouse, and controller bindings.</summary>
public sealed class InputActionMap
{
    public List<InputActionDefinition> Actions { get; set; } = [];
    [JsonIgnore] private Dictionary<string, InputActionDefinition>? _lookup;

    public static InputActionMap Load(string assetReference)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(assetReference);
        var relative = assetReference.StartsWith("project://", StringComparison.OrdinalIgnoreCase)
            ? assetReference[10..] : assetReference;
        var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
        options.Converters.Add(new JsonStringEnumConverter());
        var map = ProjectStorage.ReadAssetJson<InputActionMap>(relative, options)
            ?? throw new InvalidDataException($"Input mapping asset '{assetReference}' is empty.");
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
            new() { Name = "Recover", Bindings = [new() { Kind = InputBindingKind.Key, Key = KeyCode.R }, new() { Kind = InputBindingKind.GamepadButton, Button = GamepadButton.Y }] }
        ]
    };

    public float GetAxis(string action)
    {
        var definition = Find(action);
        if (definition is null) return 0.0f;
        float value = 0.0f;
        foreach (var binding in definition.Bindings)
        {
            var sample = binding.Kind switch
            {
                InputBindingKind.Key => Input.IsKeyDown(binding.Key) ? 1.0f : 0.0f,
                InputBindingKind.GamepadButton => Input.IsGamepadButtonDown(binding.Button, binding.Gamepad) ? 1.0f : 0.0f,
                InputBindingKind.GamepadAxis => Input.GetGamepadAxis(binding.Axis, binding.Gamepad),
                InputBindingKind.MouseButton => Input.IsMouseButtonDown(binding.MouseButton) ? 1.0f : 0.0f,
                InputBindingKind.MouseAxis => binding.MouseAxis == MouseAxis.X ? Input.MouseDelta.X : Input.MouseDelta.Y,
                _ => 0.0f
            };
            if (MathF.Abs(sample) < Math.Clamp(binding.DeadZone, 0.0f, 0.99f)) sample = 0.0f;
            value += sample * binding.Scale;
        }
        return Math.Clamp(value, -1.0f, 1.0f);
    }

    public bool IsDown(string action) => MathF.Abs(GetAxis(action)) > 0.5f;
    public bool WasPressed(string action)
    {
        var definition = Find(action);
        return definition is not null && definition.Bindings.Any(binding => binding.Kind switch
        {
            InputBindingKind.Key => Input.IsKeyPressed(binding.Key),
            InputBindingKind.GamepadButton => Input.IsGamepadButtonPressed(binding.Button, binding.Gamepad),
            InputBindingKind.MouseButton => Input.IsMouseButtonPressed(binding.MouseButton),
            _ => false
        });
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
