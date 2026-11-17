using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.FractureProbe;

// Shatters the "Rock" entity the way a mining script would; the C++ test checks the scene afterwards.
public sealed class FractureProbe : ScriptBehaviour
{
    [SerializedField] public bool Supported;
    [SerializedField] public int PieceCount = -1;
    [SerializedField] public bool PiecesAreHulls;
    [SerializedField] public float TotalMass;
    [SerializedField] public bool ShapeRoundTrips;
    [SerializedField] public string Failure = string.Empty;

    public override void OnCreate()
    {
        try
        {
            var rock = GameObject.Find("Rock") ?? throw new InvalidOperationException("No rock entity");
            Supported = Fracture.IsSupported;
            var pieces = Fracture.Shatter(rock, new ShatterSettings
            {
                Pieces = 6,
                Seed = 11,
                HasImpact = true,
                ImpactPoint = rock.WorldPosition + new Vector3(0.5f, 0.0f, 0.0f),
                ImpactDirection = new Vector3(-1.0f, 0.0f, 0.0f),
                Density = 1000.0f,
                Tag = "Debris",
            });
            PieceCount = pieces.Length;
            PiecesAreHulls = pieces.Length > 0;
            foreach (var piece in pieces)
            {
                PiecesAreHulls &= piece.GetComponent<ColliderComponent>()?.Shape == ColliderShape.ConvexHull && piece.HasTag("Debris");
                TotalMass += piece.GetComponent<RigidbodyComponent>()?.Mass ?? 0.0f;
            }

            var collider = rock.GetComponent<ColliderComponent>() ?? throw new InvalidOperationException("No rock collider");
            collider.Shape = ColliderShape.ConvexHull;
            ShapeRoundTrips = collider.Shape == ColliderShape.ConvexHull;
            collider.Shape = ColliderShape.Mesh;
            ShapeRoundTrips &= collider.Shape == ColliderShape.Mesh;
        }
        catch (Exception exception)
        {
            Failure = exception.ToString();
        }
    }
}
