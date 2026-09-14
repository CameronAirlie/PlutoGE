namespace PlutoGE.ScriptCore.Networking;

/// <summary>Game-thread replica binding. Dispose on disconnect; scene changes discard stale local IDs.</summary>
public sealed class ReplicatedScene : IDisposable
{
    private readonly EntityReplicationReplica _replica;
    private readonly Dictionary<ushort, Func<GameObject?>> _factories;
    private readonly Dictionary<ulong, GameObject> _objects = new();
    private readonly ulong _sceneGeneration;
    private bool _disposed;
    public ReplicatedScene(EntityReplicationReplica replica, IReadOnlyDictionary<ushort, Func<GameObject?>> factories)
    {
        _replica = replica;
        _factories = factories.ToDictionary(pair => pair.Key, pair => pair.Value);
        _sceneGeneration = SceneManager.RuntimeGeneration;
        if (_sceneGeneration == 0) throw new InvalidOperationException("Replication requires an active runtime scene.");
    }
    /// <summary>Copies replicated properties into game-specific components after pose application.</summary>
    public Action<GameObject, ReplicatedEntityState>? ApplyProperties { get; set; }
    public bool Update(double now)
    {
        if (_disposed) return false;
        if (SceneManager.RuntimeGeneration != _sceneGeneration)
        {
            _objects.Clear(); // IDs may now identify unrelated entities in the new scene.
            _replica.Clear();
            _disposed = true;
            return false;
        }
        var states = _replica.Entities;
        if (states.Values.Any(state => !_factories.ContainsKey(state.Type))) return false;
        foreach (var id in _objects.Keys.Where(id => !states.ContainsKey(id)).ToArray())
        {
            _objects[id].Destroy();
            _objects.Remove(id);
        }
        foreach (var (id, state) in states)
        {
            if (!_objects.TryGetValue(id, out var entity))
            {
                entity = _factories[state.Type]();
                if (entity is null) return false;
                _objects.Add(id, entity);
            }
            if (_replica.Sample(id, now, out var position, out var rotation))
            {
                entity.Position = position;
                entity.RotationQuaternion = rotation;
            }
            ApplyProperties?.Invoke(entity, state);
            if (_disposed) return false;
        }
        return true;
    }
    public void Dispose()
    {
        if (_disposed) return;
        if (SceneManager.RuntimeGeneration == _sceneGeneration)
            foreach (var entity in _objects.Values) entity.Destroy();
        _objects.Clear();
        _replica.Clear();
        _disposed = true;
    }
}
