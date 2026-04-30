using System.Numerics;

namespace PlutoGE.ScriptCore.Examples;

public sealed class RotatorScript : ScriptBehaviour
{
    [SerializedField]
    public float DegreesPerSecond = 20.0f;

    [SerializedField]
    public Vector3 Axis = new(1.0f, 1.0f, 1.0f);

    [SerializedField]
    public bool Enabled = true;

    public override void OnUpdate(float deltaTime)
    {
        if (!Enabled)
        {
            return;
        }

        var normalizedAxis = Axis.LengthSquared() > 0.0f ? Vector3.Normalize(Axis) : Vector3.UnitY;
        Rotation += normalizedAxis * DegreesPerSecond * deltaTime;
    }
}