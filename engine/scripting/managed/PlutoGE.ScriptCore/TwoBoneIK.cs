using System.Numerics;

namespace PlutoGE.ScriptCore;

/// <summary>The joint positions a two-bone chain takes to reach a target (<see cref="TwoBoneIK.Solve"/>).</summary>
public readonly record struct TwoBoneSolution(Vector3 Middle, Vector3 Tip, bool Reached);

/// <summary>
/// Analytic two-bone inverse kinematics for chains a script builds itself: procedural legs and arms made of entities,
/// a crane, a lamp. It is the geometry the engine's skeletal IK (TwoBoneIK.h) uses: the chain bends in the plane of the
/// root, the target and the pole, and stretches no further than its two lengths allow. Pure maths with no engine state,
/// so it can be called anywhere and tested on its own.
/// </summary>
public static class TwoBoneIK
{
    /// <summary>
    /// Where the middle joint (knee, elbow) and the tip (foot, hand) go for a chain rooted at <paramref name="root"/>
    /// with bones of <paramref name="upperLength"/> and <paramref name="lowerLength"/> reaching for
    /// <paramref name="target"/>. The middle joint bends toward <paramref name="pole"/> (a point in front of a knee).
    /// A target out of reach leaves the chain straight toward it, or folded as far as it can, with Reached false.
    /// </summary>
    public static TwoBoneSolution Solve(Vector3 root, float upperLength, float lowerLength, Vector3 target, Vector3 pole)
    {
        upperLength = MathF.Max(upperLength, 1e-6f);
        lowerLength = MathF.Max(lowerLength, 1e-6f);
        var toTarget = target - root;
        var length = toTarget.Length();
        var direction = length > 1e-6f ? toTarget / length : -Vector3.UnitY;

        // The bend direction: the pole's offset from the root-target line.
        var bend = pole - root;
        bend -= direction * Vector3.Dot(bend, direction);
        if (bend.LengthSquared() < 1e-12f)
            bend = Vector3.Cross(direction, MathF.Abs(direction.Y) < 0.9f ? Vector3.UnitY : Vector3.UnitX);
        bend = Vector3.Normalize(bend);

        var minimum = MathF.Max(MathF.Abs(upperLength - lowerLength), 1e-6f);
        var maximum = upperLength + lowerLength;
        var distance = Math.Clamp(length, minimum, maximum);
        // Law of cosines: how far along the root-target line the middle joint sits, and how far off it.
        var along = (upperLength * upperLength - lowerLength * lowerLength + distance * distance) / (2.0f * distance);
        var across = MathF.Sqrt(MathF.Max(0.0f, upperLength * upperLength - along * along));
        var middle = root + direction * along + bend * across;
        var tip = root + direction * distance;
        var reached = length >= minimum - 1e-4f && length <= maximum + 1e-4f;
        return new TwoBoneSolution(middle, tip, reached);
    }

    /// <summary>
    /// The rotation that turns <paramref name="from"/> to point along <paramref name="to"/> by the shortest arc, for
    /// aiming a bone (whose rest axis is <paramref name="from"/>) at the joint after it.
    /// </summary>
    public static Quaternion Between(Vector3 from, Vector3 to)
    {
        if (from.LengthSquared() < 1e-12f || to.LengthSquared() < 1e-12f)
            return Quaternion.Identity;
        from = Vector3.Normalize(from);
        to = Vector3.Normalize(to);
        var dot = Math.Clamp(Vector3.Dot(from, to), -1.0f, 1.0f);
        if (dot > 0.999999f)
            return Quaternion.Identity;
        if (dot < -0.999999f)
        {
            var axis = Vector3.Cross(from, MathF.Abs(from.X) < 0.9f ? Vector3.UnitX : Vector3.UnitY);
            return Quaternion.CreateFromAxisAngle(Vector3.Normalize(axis), MathF.PI);
        }
        var cross = Vector3.Cross(from, to);
        return Quaternion.Normalize(new Quaternion(cross, 1.0f + dot));
    }
}
