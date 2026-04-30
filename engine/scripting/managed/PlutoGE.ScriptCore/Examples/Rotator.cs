namespace PlutoGE.ScriptCore.Examples;

public sealed class Rotator : ScriptBehaviour
{
    [SerializedField]
    public float DegreesPerSecond = 90.0f;

    [SerializedField]
    public bool Enabled = true;

    public override void OnUpdate(float deltaTime)
    {
        if (!Enabled)
        {
            return;
        }

        // The native runtime will eventually bridge transform mutation back to the engine.
    }
}