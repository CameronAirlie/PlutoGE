using System;
using System.Numerics;

namespace RocketLeg.Scripts;

/// <summary>
/// Kickoff and respawn placements. Positions are authored for Blue (defending
/// -X); Orange uses the point-mirrored spot, so both cars are always the same
/// distance from the ball. Every rotation faces the centre spot.
/// </summary>
public static class KickoffLayout
{
    public const float CarHeight = 0.75f;

    // Diagonal left/right, off-centre left/right, and straight back.
    private static readonly Vector2[] BlueKickoffs =
    {
        new(-30.0f, -24.0f), new(-30.0f, 24.0f), new(-41.0f, -8.0f), new(-41.0f, 8.0f), new(-46.0f, 0.0f),
    };

    // Beside the goal box, so a respawned car can rejoin defence at once.
    private static readonly Vector2[] BlueRespawns =
    {
        new(-(ArenaLayout.FloorHalfLength - 4.0f), -(ArenaLayout.GoalHalfWidth + 8.0f)),
        new(-(ArenaLayout.FloorHalfLength - 4.0f), ArenaLayout.GoalHalfWidth + 8.0f),
    };

    public static int KickoffCount => BlueKickoffs.Length;

    public static (Vector3 Position, Vector3 Rotation) Kickoff(Team team, int slot)
    {
        var spot = BlueKickoffs[Math.Clamp(slot, 0, BlueKickoffs.Length - 1)];
        return Place(team == Team.Blue ? spot : -spot, Vector2.Zero);
    }

    /// <summary>A demolished car returns near its own goal, facing upfield.</summary>
    public static (Vector3 Position, Vector3 Rotation) Respawn(Team team, int slot)
    {
        var spot = BlueRespawns[Math.Abs(slot) % BlueRespawns.Length];
        if (team == Team.Orange) spot = -spot;
        return Place(spot, new Vector2(0.0f, spot.Y));
    }

    private static (Vector3, Vector3) Place(Vector2 spot, Vector2 lookAt)
    {
        var direction = lookAt - spot;
        // Entity forward is -Z: yaw a turns it to (-sin a, 0, -cos a).
        var yaw = MathF.Atan2(-direction.X, -direction.Y) * 180.0f / MathF.PI;
        return (new Vector3(spot.X, CarHeight, spot.Y), new Vector3(0.0f, yaw, 0.0f));
    }
}
