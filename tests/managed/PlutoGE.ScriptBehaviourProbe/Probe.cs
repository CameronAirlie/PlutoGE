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
