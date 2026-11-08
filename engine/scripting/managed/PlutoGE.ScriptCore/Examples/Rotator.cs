using System.Numerics;

namespace PlutoGE.ScriptCore.Examples;

public sealed class Rotator : ScriptBehaviour
{
    [SerializedField]
    public float DegreesPerSecond = 90.0f;

    // Kept separate from ScriptBehaviour.Enabled so a stopped rotator still receives collisions.
    [SerializedField, FormerlySerializedAs("Enabled")]
    public bool Spinning = true;

    public override void OnUpdate(float deltaTime)
    {
    }

    public override void OnLateUpdate(float deltaTime)
    {
        if (!Spinning)
        {
            return;
        }

        GameObject.Rotation = GameObject.Rotation + new Vector3(0f, DegreesPerSecond * deltaTime, 0f);
    }

    private bool _hasCollided = false;

    public override void OnCollisionEnter(GameObject other)
    {
        base.OnCollisionEnter(other);

        if (_hasCollided)
        {
            return;
        }

        Debug.Log($"Rotator collided with {other.Name}.");
        if (other.GetComponent<Rotator>() is Rotator otherRotator)
        {
            Debug.Log($"Collided with {other.Name}, toggling Spinning state for both Rotators.");
            Spinning = !Spinning;
            otherRotator.Spinning = !otherRotator.Spinning;
            _hasCollided = true;
        }
    }

    public override void OnCollisionExit(GameObject other)
    {
        base.OnCollisionExit(other);

        if (_hasCollided)
        {
            Debug.Log($"Rotator exited collision with {other.Name}, resetting collision state.");
            _hasCollided = false;
        }
    }

}