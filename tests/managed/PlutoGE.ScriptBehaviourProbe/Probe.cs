using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptBehaviourProbe;

// Probes for ScriptBehaviour lifecycle behaviour; ScriptBehaviourTests.cpp drives them and reads their fields.

// OnCreate throws, so the instance is faulted and must not receive updates.
public sealed class CreateFaultProbe : ScriptBehaviour
{
    [SerializedField] public int Updates;

    public override void OnCreate() => throw new InvalidOperationException("probe create failure");

    public override void OnUpdate(float deltaTime) => Updates++;
}

// OnUpdate throws every frame; the error must be reported, throttled, and updates must keep running.
public sealed class UpdateFaultProbe : ScriptBehaviour
{
    [SerializedField] public int Updates;

    public override void OnUpdate(float deltaTime)
    {
        Updates++;
        Fail();
    }

    private static void Fail() => throw new InvalidOperationException("probe update failure");
}

// Two-phase start: SiblingProbe is created first, yet its OnCreate must see PartnerProbe with fields applied.
public sealed class SiblingProbe : ScriptBehaviour
{
    [SerializedField] public bool SawPartner;

    public override void OnCreate()
    {
        var partner = GameObject.Find("Partner")?.GetComponent<PartnerProbe>();
        SawPartner = partner is { Configured: 5 };
    }
}

public sealed class PartnerProbe : ScriptBehaviour
{
    [SerializedField] public int Configured;
}

// Spawning: position, parent and configure must all be in place before the spawned script's OnCreate.
public sealed class SpawnerProbe : ScriptBehaviour
{
    [SerializedField] public string PrefabReference = string.Empty;
    [SerializedField] public bool SpawnWorked;
    [SerializedField] public string Failure = string.Empty;

    public override void OnCreate()
    {
        var spawned = Prefab.Instantiate<SpawnedProbe>(PrefabReference, new System.Numerics.Vector3(3, 4, 5),
            System.Numerics.Quaternion.Identity, probe => probe.Damage = 42, GameObject);
        if (spawned is null)
        {
            Failure = "Instantiate<SpawnedProbe> returned null";
            return;
        }

        SpawnWorked = spawned.SeenDamage == 42 && spawned.SeenPosition == new System.Numerics.Vector3(3, 4, 5) &&
                      spawned.SeenParent == EntityId;
        if (!SpawnWorked)
        {
            Failure = $"OnCreate saw damage {spawned.SeenDamage}, position {spawned.SeenPosition}, parent {spawned.SeenParent}";
        }
    }
}

public sealed class SpawnedProbe : ScriptBehaviour
{
    public int Damage;
    public int SeenDamage = -1;
    public System.Numerics.Vector3 SeenPosition;
    public uint SeenParent;

    public override void OnCreate()
    {
        SeenDamage = Damage;
        SeenPosition = GameObject.Position;
        SeenParent = GameObject.Parent?.EntityId ?? 0;
    }
}

public enum ProbeMode
{
    Idle,
    Patrol,
    Attack,
}

// Every serialized field feature: new value types, arrays of plain values and the inspector attributes.
public sealed class FieldTypesProbe : ScriptBehaviour
{
    [SerializedField, Header("Behaviour"), Tooltip("What the probe does")] public ProbeMode Mode = ProbeMode.Patrol;
    [SerializedField, Range(0, 10)] public float Speed = 2.5f;
    [SerializedField] public Color Tint = new(1, 0.5f, 0.25f, 1);
    [SerializedField] public System.Numerics.Vector4 Weights = new(1, 2, 3, 4);
    [SerializedField] public System.Numerics.Quaternion Facing = System.Numerics.Quaternion.Identity;
    [SerializedField] public int[] Levels = [1, 2, 3];
    [SerializedField] public List<string> Names = ["a", "b"];
    [SerializedField] public ProbeMode[] Sequence = [ProbeMode.Idle, ProbeMode.Attack];
    [SerializedField, FormerlySerializedAs("oldCount")] public int Count = 1;
    [SerializedField] public Dictionary<string, int> Unsupported = new();
}

// Time and timers across frames at time scale 1, then 0.
public sealed class TimerProbe : ScriptBehaviour
{
    [SerializedField] public int OnceFired;
    [SerializedField] public int RepeatFired;
    [SerializedField] public int UnscaledFired;
    [SerializedField] public int Frames;
    [SerializedField] public float SeenTime;
    [SerializedField] public float SeenUnscaledTime;
    [SerializedField] public bool CancelledFired;

    public override void OnCreate()
    {
        After(0.1f, () => OnceFired++);
        Every(0.05f, () => RepeatFired++);
        After(0.01f, () => CancelledFired = true).Cancel();
        Timers.After(0.3f, () => UnscaledFired++, this, unscaled: true);
    }

    public override void OnUpdate(float deltaTime)
    {
        Frames = (int)Time.FrameCount;
        SeenTime = (float)Time.TimeSinceStart;
        SeenUnscaledTime = (float)Time.UnscaledTimeSinceStart;
    }
}

// Enabled: disables itself after two updates; EnableControllerProbe turns it back on later.
public sealed class EnableProbe : ScriptBehaviour
{
    [SerializedField] public int Updates;
    [SerializedField] public int Enables;
    [SerializedField] public int Disables;

    public override void OnEnable() => Enables++;
    public override void OnDisable() => Disables++;

    public override void OnUpdate(float deltaTime)
    {
        Updates++;
        if (Updates == 2)
        {
            Enabled = false;
        }
    }
}

public sealed class EnableControllerProbe : ScriptBehaviour
{
    private int _frames;

    public override void OnUpdate(float deltaTime)
    {
        if (++_frames == 4 && GameObject.Find("Toggled")?.GetComponent<EnableProbe>() is { } target)
        {
            target.Enabled = true;
        }
    }
}

// Physics queries against a ground slab and a tagged crate.
public sealed class PhysicsQueryProbe : ScriptBehaviour
{
    [SerializedField] public string Failure = string.Empty;

    public override void OnCreate()
    {
        var crate = System.Numerics.Vector3.UnitX * 3 + System.Numerics.Vector3.UnitY * 0.5f;
        var near = Physics.OverlapSphere(crate, 0.6f);
        if (!near.Any(hit => hit.Name == "Crate"))
            Failure += "OverlapSphere missed the crate. ";
        if (Physics.OverlapSphere(crate, 0.6f, tag: "Missing").Length != 0)
            Failure += "Tag filter let an untagged collider through. ";
        if (!Physics.OverlapBox(crate, new(0.2f, 0.2f, 0.2f), System.Numerics.Quaternion.Identity, tag: "Crate").Any(hit => hit.Name == "Crate"))
            Failure += "OverlapBox missed the crate. ";
        var hits = Physics.RaycastAll(new(-5, 0.5f, 0), System.Numerics.Vector3.UnitX, 20);
        if (hits.Length == 0 || hits[0].Entity.Name != "Crate" || hits.Any(hit => hit.Entity.Name == "Zone"))
            Failure += $"RaycastAll returned {hits.Length} hits. ";
        if (!Physics.SphereCast(new(3, 0.5f, -5), System.Numerics.Vector3.UnitZ, 0.25f, 10, out var cast) ||
            cast.Entity.Name != "Crate" || MathF.Abs(cast.Distance - 4.25f) > 0.05f)
            Failure += $"SphereCast hit {cast.Entity?.Name} at {cast.Distance}. ";
    }
}

// Collision contact data and trigger callbacks on a falling ball.
public sealed class FallingBallProbe : ScriptBehaviour
{
    [SerializedField] public string HitName = string.Empty;
    [SerializedField] public float NormalY;
    [SerializedField] public float Impulse;
    [SerializedField] public int TriggerEnters;
    [SerializedField] public int TriggerExits;

    public override void OnCollisionEnter(Collision collision)
    {
        HitName = collision.Other.Name;
        NormalY = collision.Normal.Y;
        Impulse = collision.Impulse;
    }

    public override void OnTriggerEnter(GameObject other) => TriggerEnters++;
    public override void OnTriggerExit(GameObject other) => TriggerExits++;
}

public sealed class TriggerZoneProbe : ScriptBehaviour
{
    [SerializedField] public string Entered = string.Empty;
    [SerializedField] public int Exits;

    public override void OnTriggerEnter(GameObject other) => Entered = other.Name;
    public override void OnTriggerExit(GameObject other) => Exits++;
}
