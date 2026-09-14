namespace PlutoGE.ScriptCore.Networking;

/// <summary>Game-thread replica binding. Dispose on disconnect; scene changes discard stale local IDs.</summary>
public sealed class ReplicatedScene : IDisposable
{
    private readonly EntityReplicationReplica _replica;
    private readonly Dictionary<ushort, Func<GameObject?>> _factories;
    private readonly Dictionary<ulong, GameObject> _objects = new();
    private readonly ulong _sceneGeneration;
    private bool _disposed;
    private bool _updating;
    public ReplicatedScene(EntityReplicationReplica replica, IReadOnlyDictionary<ushort, Func<GameObject?>> factories)
    {
        ArgumentNullException.ThrowIfNull(replica);
        ArgumentNullException.ThrowIfNull(factories);
        if (factories.Any(pair => pair.Key == 0 || pair.Value is null))
            throw new ArgumentException("Factories require nonzero type IDs and nonnull functions.", nameof(factories));
        _replica = replica;
        _factories = factories.ToDictionary(pair => pair.Key, pair => pair.Value);
        _sceneGeneration = SceneManager.RuntimeGeneration;
        if (_sceneGeneration == 0) throw new InvalidOperationException("Replication requires an active runtime scene.");
    }
    /// <summary>Copies replicated properties into game-specific components after pose application.</summary>
    public Action<GameObject, ReplicatedEntityState>? ApplyProperties { get; set; }
    public bool Update(double now)
    {
        if (_updating || !double.IsFinite(now) || now < 0 || !ValidateLifetime()) return false;
        _updating = true;
        try { return Apply(now); }
        finally { _updating = false; }
    }

    private bool ValidateLifetime()
    {
        if (_disposed) return false;
        if (SceneManager.RuntimeGeneration != _sceneGeneration)
        {
            _objects.Clear(); // IDs may now identify unrelated entities in the new scene.
            _replica.Clear();
            _disposed = true;
            return false;
        }
        return true;
    }

    private bool Apply(double now)
    {
        var states = _replica.Entities;
        if (states.Values.Any(state => !_factories.ContainsKey(state.Type))) return false;
        foreach (var id in _objects.Keys.Where(id => !states.ContainsKey(id)).ToArray())
        {
            _objects[id].Destroy();
            _objects.Remove(id);
            if (!ValidateLifetime()) return false;
        }
        foreach (var (id, state) in states)
        {
            if (!_objects.TryGetValue(id, out var entity))
            {
                entity = _factories[state.Type]();
                if (!ValidateLifetime())
                {
                    // A factory may dispose the binding while creating its final
                    // object. Only reclaim it if native IDs still belong to us.
                    if (SceneManager.RuntimeGeneration == _sceneGeneration) entity?.Destroy();
                    return false;
                }
                if (entity is null || !entity.IsValid || _objects.Values.Any(existing => existing.EntityId == entity.EntityId)) return false;
                _objects.Add(id, entity);
            }
            if (_replica.Sample(id, now, out var position, out var rotation))
            {
                entity.Position = position;
                entity.RotationQuaternion = rotation;
            }
            ApplyProperties?.Invoke(entity, state);
            if (!ValidateLifetime()) return false;
        }
        return true;
    }
    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        foreach (var entity in _objects.Values.ToArray())
        {
            if (SceneManager.RuntimeGeneration != _sceneGeneration) break;
            entity.Destroy();
        }
        _objects.Clear();
        _replica.Clear();
    }
}
