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
