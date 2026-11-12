using System.Numerics;
using PlutoGE.ScriptCore;

// TwoBoneIK: the managed analytic solver procedural rigs (animal legs) use.
static class Program
{
    static void Require(bool condition, string message)
    {
        if (!condition)
            throw new Exception(message);
    }

    static bool Near(Vector3 a, Vector3 b, float tolerance = 1e-4f) => Vector3.Distance(a, b) <= tolerance;

    static int Main()
    {
        var hip = new Vector3(0, 1, 0);
        var pole = new Vector3(0, 0.5f, 1); // knees bend forward (+Z)

        // A reachable foot: both bones keep their lengths, the foot lands on the target, the knee bends toward the pole.
        var solution = TwoBoneIK.Solve(hip, 0.5f, 0.5f, new Vector3(0, 0.3f, 0), pole);
        Require(solution.Reached, "A target within reach is reached");
        Require(Near(solution.Tip, new Vector3(0, 0.3f, 0)), "The tip lands on the target");
        Require(MathF.Abs(Vector3.Distance(hip, solution.Middle) - 0.5f) < 1e-4f &&
                MathF.Abs(Vector3.Distance(solution.Middle, solution.Tip) - 0.5f) < 1e-4f, "Bones keep their lengths");
        Require(solution.Middle.Z > 0.1f, "The knee bends toward the pole");

        // Out of reach: straight toward the target, not reached.
        var far = TwoBoneIK.Solve(hip, 0.5f, 0.5f, new Vector3(0, -5, 0), pole);
        Require(!far.Reached && Near(far.Tip, new Vector3(0, 0, 0)) && Near(far.Middle, new Vector3(0, 0.5f, 0)),
                "An unreachable target straightens the chain toward it");

        // Too close: folded as far as the bones allow.
        var near = TwoBoneIK.Solve(hip, 0.6f, 0.4f, hip, pole);
        Require(!near.Reached && MathF.Abs(Vector3.Distance(hip, near.Tip) - 0.2f) < 1e-3f, "A target at the root folds the chain");

        // A pole on the line still bends somewhere sensible, never NaN.
        var degenerate = TwoBoneIK.Solve(hip, 0.5f, 0.5f, new Vector3(0, 0.3f, 0), new Vector3(0, -3, 0));
        Require(float.IsFinite(degenerate.Middle.X + degenerate.Middle.Y + degenerate.Middle.Z), "A degenerate pole is finite");

        // Between aims a bone's rest axis along a direction.
        var turn = TwoBoneIK.Between(-Vector3.UnitY, Vector3.UnitZ);
        Require(Near(Vector3.Transform(-Vector3.UnitY, turn), Vector3.UnitZ), "Between turns from onto to");
        Require(Near(Vector3.Transform(Vector3.UnitX, TwoBoneIK.Between(Vector3.UnitX, -Vector3.UnitX)), -Vector3.UnitX),
                "Between handles opposite directions");
        Require(TwoBoneIK.Between(Vector3.UnitY, Vector3.UnitY) == Quaternion.Identity, "Between of equal directions is identity");

        Console.WriteLine("TwoBoneIK smoke tests passed");
        return 0;
    }
}
