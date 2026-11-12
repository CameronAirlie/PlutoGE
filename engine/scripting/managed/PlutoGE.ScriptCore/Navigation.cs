using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public readonly struct NavigationPath
{
    internal NavigationPath(Vector3[] points, bool complete)
    {
        Points = points;
        Complete = complete;
    }

    public IReadOnlyList<Vector3> Points { get; }
    public bool Complete { get; }
}

public static class Navigation
{
    public static bool ProjectPoint(GameObject navigationMesh, Vector3 point, out Vector3 projected,
        float agentRadius = 0.0f, float agentHeight = 0.0f)
    {
        projected = default;
        return navigationMesh is not null && navigationMesh.IsValid &&
               ScriptBridge.NavigationProjectPoint(navigationMesh.EntityId, point,
                   Math.Max(0.0f, agentRadius), Math.Max(0.0f, agentHeight), out projected);
    }

    /// <summary>Bakes the navigation mesh on its next update: at once, or over frames when it has a bake time slice
    /// (the previous bake keeps answering meanwhile).</summary>
    public static bool RequestBake(GameObject navigationMesh) =>
        Control(navigationMesh, ScriptBridge.NavigationControlOp.RequestBake) != 0;

    /// <summary>Re-samples a world-space box on the mesh's next update: call it when something is built, removed,
    /// opened or closed there. Cheap for boxes of a few metres.</summary>
    public static bool InvalidateRegion(GameObject navigationMesh, Vector3 boundsMin, Vector3 boundsMax) =>
        Control(navigationMesh, ScriptBridge.NavigationControlOp.InvalidateRegion, boundsMin, boundsMax) != 0;

    public static bool IsBaked(GameObject navigationMesh) =>
        Control(navigationMesh, ScriptBridge.NavigationControlOp.IsBaked) != 0;

    public static bool IsBaking(GameObject navigationMesh) =>
        Control(navigationMesh, ScriptBridge.NavigationControlOp.IsBaking) != 0;

    /// <summary>Changes whenever the mesh's walkable data does; a path planned on an older revision may be stale.</summary>
    public static long Revision(GameObject navigationMesh) =>
        Control(navigationMesh, ScriptBridge.NavigationControlOp.Revision);

    /// <summary>Sets the world-space box the next bake covers, and its cell size when cellSize is above zero.</summary>
    public static bool SetBounds(GameObject navigationMesh, Vector3 boundsMin, Vector3 boundsMax, float cellSize = 0.0f) =>
        Control(navigationMesh, ScriptBridge.NavigationControlOp.SetBounds, boundsMin, boundsMax, cellSize) != 0;

    /// <summary>Milliseconds of bake work per frame for requested bakes; 0 bakes in one go.</summary>
    public static bool SetBakeTimeSlice(GameObject navigationMesh, float milliseconds) =>
        Control(navigationMesh, ScriptBridge.NavigationControlOp.SetBakeTimeSlice, value: milliseconds) != 0;

    private static long Control(GameObject navigationMesh, ScriptBridge.NavigationControlOp operation,
        Vector3 a = default, Vector3 b = default, float value = 0.0f) =>
        navigationMesh is not null && navigationMesh.IsValid
            ? ScriptBridge.NavigationControl(navigationMesh.EntityId, operation, a, b, value)
            : 0;

    public static NavigationPath FindPath(GameObject navigationMesh, Vector3 start, Vector3 end,
        float agentRadius = 0.0f, float agentHeight = 0.0f)
    {
        if (navigationMesh is null || !navigationMesh.IsValid)
            return new NavigationPath([], false);
        var points = ScriptBridge.NavigationFindPath(navigationMesh.EntityId, start, end,
            Math.Max(0.0f, agentRadius), Math.Max(0.0f, agentHeight), out var complete);
        return new NavigationPath(points, complete);
    }
}
