using System;
using System.Numerics;

namespace RocketLeg.Scripts;

/// <summary>
/// Stadium dimensions in metres. The meshes and goal pieces are generated from
/// the same numbers by <c>generate_arena.py</c>; change both together.
/// <para>
/// The flat floor is a rounded rectangle (half extents
/// <see cref="FloorHalfLength"/> x <see cref="FloorHalfWidth"/>, plan corners
/// of <see cref="CornerRadius"/>). Curved banks of radius
/// <see cref="BankRadius"/> rise to vertical walls, which curve over into the
/// ceiling at <see cref="CeilingHeight"/>. Goals are recessed into the end
/// walls; their mouths lie on the planes x = +/-<see cref="GoalLine"/>.
/// </para>
/// </summary>
public static class ArenaLayout
{
    public const float FloorHalfLength = 60.0f;
    public const float FloorHalfWidth = 41.0f;
    public const float CornerRadius = 10.0f;
    public const float BankRadius = 7.0f;
    public const float WallTop = 23.0f;
    public const float CeilingHeight = WallTop + BankRadius;

    public const float GoalLine = FloorHalfLength + BankRadius;
    public const float GoalHalfWidth = 12.0f;
    public const float GoalHeight = 9.0f;
    public const float GoalDepth = 10.0f;

    /// <summary>
    /// Whether a body centre has ended up beyond the stadium shell. The shell is
    /// a thin triangle mesh; a body whose centre crosses it is pushed out of the
    /// wrong side instead of back in. When this returns true,
    /// <paramref name="corrected"/> is the nearest point inside the shell, at
    /// <paramref name="clearance"/> from the surface, and <paramref name="inward"/>
    /// is the surface normal there. Goals are skipped: their box colliders are solid.
    /// </summary>
    public static bool TryContain(Vector3 position, float clearance, out Vector3 corrected, out Vector3 inward)
    {
        corrected = position;
        inward = Vector3.Zero;
        if (MathF.Abs(position.Z) < GoalHalfWidth + 1.0f && MathF.Abs(position.X) > FloorHalfLength - 1.0f &&
            position.Y < GoalHeight + 1.0f)
            return false;

        // Plan view: offset beyond the rectangle of corner centres.
        var dx = MathF.Max(MathF.Abs(position.X) - (FloorHalfLength - CornerRadius), 0.0f);
        var dz = MathF.Max(MathF.Abs(position.Z) - (FloorHalfWidth - CornerRadius), 0.0f);
        var r = MathF.Sqrt(dx * dx + dz * dz);
        if (r <= CornerRadius)
        {
            if (position.Y <= CeilingHeight) return false;
            corrected.Y = CeilingHeight - clearance;
            inward = -Vector3.UnitY;
            return true;
        }

        // Profile (r, y): inside means within BankRadius of the segment that
        // runs from the lower bank's centre up to the roof curve's centre.
        var segmentY = Math.Clamp(position.Y, BankRadius, WallTop);
        var outR = r - CornerRadius;
        var outY = position.Y - segmentY;
        var distance = MathF.Sqrt(outR * outR + outY * outY);
        if (distance <= BankRadius) return false;

        var unitR = outR / distance;
        var unitY = outY / distance;
        var depth = BankRadius - clearance;
        var newR = CornerRadius + unitR * depth;
        var planX = dx / r;
        var planZ = dz / r;
        if (dx > 0.0f) corrected.X = MathF.Sign(position.X) * (FloorHalfLength - CornerRadius + planX * newR);
        if (dz > 0.0f) corrected.Z = MathF.Sign(position.Z) * (FloorHalfWidth - CornerRadius + planZ * newR);
        corrected.Y = segmentY + unitY * depth;
        inward = -Vector3.Normalize(new Vector3(MathF.Sign(position.X) * planX * unitR, unitY,
                                                MathF.Sign(position.Z) * planZ * unitR));
        return true;
    }
}
