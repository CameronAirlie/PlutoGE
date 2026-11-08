using System.Numerics;

namespace PlutoGE.ScriptCore;

/// <summary>A collision as seen by the script receiving <see cref="ScriptBehaviour.OnCollisionEnter(Collision)"/>.</summary>
public readonly struct Collision
{
    internal Collision(GameObject other, Vector3 point, Vector3 normal, float impulse)
    {
        Other = other;
        Point = point;
        Normal = normal;
        Impulse = impulse;
    }

    /// <summary>The entity whose collider was hit.</summary>
    public GameObject Other { get; }

    /// <summary>World position of the deepest contact point.</summary>
    public Vector3 Point { get; }

    /// <summary>The other collider's surface normal at the contact, pointing towards this entity.</summary>
    public Vector3 Normal { get; }

    /// <summary>Impulse the physics solver applied in the first step of contact (N·s); 0 for kinematic contacts.</summary>
    public float Impulse { get; }
}
