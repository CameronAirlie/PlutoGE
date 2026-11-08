using System.Numerics;

namespace PlutoGE.ScriptCore.Examples;

public sealed class RotatorScript : ScriptBehaviour
{
    [SerializedField]
    public float DegreesPerSecond = 20.0f;

    [SerializedField]
    public Vector3 Axis = new(1.0f, 1.0f, 1.0f);

    // A pause switch for this example; older scenes saved it as "Enabled".
    [SerializedField, FormerlySerializedAs("Enabled")]
    public bool Spinning = true;

    public override void OnUpdate(float deltaTime)
    {
        if (!Spinning)
        {
            return;
        }

        var normalizedAxis = Axis.LengthSquared() > 0.0f ? Vector3.Normalize(Axis) : Vector3.UnitY;
        Rotation += normalizedAxis * DegreesPerSecond * deltaTime;
    }
}