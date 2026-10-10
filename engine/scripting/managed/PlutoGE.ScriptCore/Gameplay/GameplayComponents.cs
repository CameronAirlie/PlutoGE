using System.Numerics;
using System.Text.Json;
using PlutoGE.ScriptCore.Authoring;

namespace PlutoGE.ScriptCore.Gameplay;

/// <summary>Inspector-ready health; send Damage(float), Heal(float), or Respawn() through actions.</summary>
public sealed class HealthBehaviour : ScriptBehaviour
{
    [SerializedField] public float Maximum = 100;
    [SerializedField] public GameObject? DeathTarget;
    [SerializedField] public string DeathMethod = "Open";
    public Health Model { get; private set; } = new();
    public override void OnCreate()
    {
        Model = new Health(Maximum);
        Model.Died += OnDied;
    }
    public override void OnDestroy() => Model.Died -= OnDied;
    private void OnDied() => DeathTarget?.TryInvoke(DeathMethod);
    public void Damage(float amount) => Model.Damage(amount);
    public void Heal(float amount) => Model.Heal(amount);
    public void Respawn() => Model.Set(Model.Maximum);
}

public sealed class InventoryBehaviour : ScriptBehaviour
{
    [SerializedField] public int Capacity = 20;
    public Inventory Model { get; private set; } = new();
    public override void OnCreate() => Model = new Inventory(Capacity);
}

public sealed class PickupBehaviour : ScriptBehaviour
{
    [SerializedField] public string Item = "key";
    [SerializedField] public int Amount = 1;
    [SerializedField] public GameObject? Receiver;
    public override void OnCreate() => Receiver ??= GameObject.FindWithTag("Player");
    public void Interact()
    {
        var inventory = Receiver?.GetComponent<InventoryBehaviour>();
        if (inventory?.Model.Add(Item, Amount) == true) GameObject.Active = false;
    }
}

public sealed class DoorBehaviour : ScriptBehaviour
{
    [SerializedField] public Vector3 OpenOffset = new(0, 3, 0);
    [SerializedField] public float Speed = 3;
    private Vector3 _closed;
    private bool _open;
    public override void OnCreate() { _closed = GameObject.Position; _open = false; }
    public void Open() => _open = true;
    public void Close() => _open = false;
    public void Interact() => _open = !_open;
    public override void OnUpdate(float deltaTime)
    {
        if (!float.IsFinite(Speed) || Speed < 0 || deltaTime <= 0) return;
        var target = _closed + (_open ? OpenOffset : Vector3.Zero);
        var offset = target - GameObject.Position;
        var length = offset.Length();
        if (length > 0) GameObject.Position += offset * Math.Min(1, Speed * deltaTime / length);
    }
}

/// <summary>Collision-filtered checkpoint; assign a player and call Respawn from a death action.</summary>
public sealed class CheckpointBehaviour : ScriptBehaviour
{
    [SerializedField] public GameObject? Player;
    private Vector3 _position;
    private Vector3 _rotation;
    public override void OnCreate() => Capture();
    public void Capture()
    {
        if (Player is null) return;
        _position = Player.WorldPosition;
        _rotation = Player.WorldRotation;
    }
    public override void OnCollisionEnter(GameObject other) { if (other.EntityId == Player?.EntityId) Capture(); }
    public void Respawn()
    {
        if (Player is null) return;
        Player.WorldPosition = _position;
        Player.WorldRotation = _rotation;
        Player.GetComponent<HealthBehaviour>()?.Respawn();
    }
}

/// <summary>Authored event/action connection. Source: Key, CollisionEnter, CollisionExit, or RmlClick.</summary>
public sealed class ActionLinkBehaviour : ScriptBehaviour
{
    [SerializedField] public string Source = "Key";
    [SerializedField] public string Key = "E";
    [SerializedField] public GameObject? Target;
    [SerializedField] public string Method = "Interact";
    [SerializedField] public GameObject? CollisionFilter;
    [SerializedField] public string Document = "UI/screen.rml";
    [SerializedField] public string Element = "continue";
    [SerializedField] public GameObject? Interactor;
    [SerializedField] public float MaximumDistance = 3;
    private KeyCode _key;
    private bool _hasKey;
    private RmlDocument? _document;
    private RmlEvent? _click;
    public override void OnCreate()
    {
        Interactor ??= GameObject.FindWithTag("Player");
        _hasKey = Enum.TryParse(Key, true, out _key) && Enum.IsDefined(_key);
        if (Source == "RmlClick")
        {
            _document = new RmlDocument(Document);
            _click = _document.OnClick(Element, Invoke);
        }
    }
    public void Invoke()
    {
        if (Target is not null && !Target.TryInvoke(Method)) Debug.LogWarning($"Action target has no method '{Method}'.");
    }
    public override void OnUpdate(float deltaTime)
    {
        if (Source != "Key" || !_hasKey || GamePause.IsPaused || !Input.IsKeyPressed(_key)) return;
        if (Interactor is not null && Target is not null &&
            (!float.IsFinite(MaximumDistance) || MaximumDistance < 0 || Vector3.Distance(Interactor.WorldPosition, Target.WorldPosition) > MaximumDistance)) return;
        Invoke();
    }
    public override void OnCollisionEnter(GameObject other) { if (Source == "CollisionEnter" && Matches(other)) Invoke(); }
    public override void OnCollisionExit(GameObject other) { if (Source == "CollisionExit" && Matches(other)) Invoke(); }
    private bool Matches(GameObject other) => CollisionFilter is null || CollisionFilter.EntityId == other.EntityId;
    public override void OnDestroy()
    {
        if (_click is not null) _click.Triggered -= Invoke;
        _click = null;
        // Do not hide a document owned by another controller.
        _document = null;
    }
}

/// <summary>Reusable transform/active state participant. Register explicitly with a session-owned SaveGameService.</summary>
public sealed class PersistentObjectBehaviour : ScriptBehaviour, ISaveParticipant
{
    [SerializedField] public string Identity = "";
    public string SaveKey => Identity;
    private sealed record State(float[] Position, float[] Rotation, float[] Scale, bool Active, float? Health = null, Dictionary<string, int>? Items = null);
    private static float[] Array(Vector3 value) => [value.X, value.Y, value.Z];
    private static Vector3 Vector(float[] value) => new(value[0], value[1], value[2]);
    public JsonElement Capture() => JsonSerializer.SerializeToElement(new State(Array(GameObject.Position), Array(GameObject.Rotation), Array(GameObject.Scale), GameObject.Active,
        GameObject.GetComponent<HealthBehaviour>()?.Model.Current.Value,
        GameObject.GetComponent<InventoryBehaviour>() is { } inventory ? new Dictionary<string, int>(inventory.Model.Items) : null));
    public void Validate(JsonElement state)
    {
        var value = state.Deserialize<State>() ?? throw new JsonException("Missing transform.");
        foreach (var vector in new[] { value.Position, value.Rotation, value.Scale })
            if (vector is null || vector.Length != 3 || vector.Any(v => !float.IsFinite(v))) throw new JsonException("Invalid transform.");
        if (value.Health is { } health && (!float.IsFinite(health) || health < 0 || GameObject.GetComponent<HealthBehaviour>() is not { } targetHealth || health > targetHealth.Model.Maximum))
            throw new JsonException("Invalid health state or missing health component.");
        if (value.Items is not null)
        {
            var inventory = GameObject.GetComponent<InventoryBehaviour>() ?? throw new JsonException("Missing inventory component.");
            new Inventory(inventory.Capacity).Replace(value.Items);
        }
    }
    public void Restore(JsonElement state)
    {
        Validate(state);
        var value = state.Deserialize<State>()!;
        GameObject.Position = Vector(value.Position);
        GameObject.Rotation = Vector(value.Rotation);
        GameObject.Scale = Vector(value.Scale);
        GameObject.Active = value.Active;
        if (value.Health is { } health) GameObject.GetComponent<HealthBehaviour>()!.Model.Set(health, notifyDeath: false);
        if (value.Items is not null) GameObject.GetComponent<InventoryBehaviour>()!.Model.Replace(value.Items);
    }
}
