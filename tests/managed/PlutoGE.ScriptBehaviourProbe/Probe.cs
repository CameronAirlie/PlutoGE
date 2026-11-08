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
