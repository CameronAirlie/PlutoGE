using System.Numerics;

namespace PlutoGE.ScriptCore.Examples;

public sealed class Rotator : ScriptBehaviour
{
    [SerializedField]
    public float DegreesPerSecond = 90.0f;

    [SerializedField]
    public bool Enabled = true;

    public override void OnUpdate(float deltaTime)
    {
    }

    public override void OnLateUpdate(float deltaTime)
    {
        if (!Enabled)
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
            Debug.Log($"Collided with {other.Name}, toggling Enabled state for both Rotators.");
            Enabled = !Enabled;
            otherRotator.Enabled = !otherRotator.Enabled;
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