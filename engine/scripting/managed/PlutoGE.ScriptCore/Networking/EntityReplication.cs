using System.Collections.ObjectModel;
using System.Numerics;

namespace PlutoGE.ScriptCore.Networking;

/// <summary>Value-owned replicated state. Type is a locally registered prefab ID, never a remote asset path.</summary>
public sealed class ReplicatedEntityState
{
    public ulong Id { get; }
    public ushort Type { get; }
    public int Owner { get; }
    public ulong Section { get; }
    public Vector3 Position { get; }
    public Quaternion Rotation { get; }
    public IReadOnlyDictionary<ushort, float> Properties { get; }
    public ReplicatedEntityState(ulong id, ushort type, int owner, ulong section, Vector3 position,
        Quaternion rotation, IReadOnlyDictionary<ushort, float>? properties = null)
    {
        if (id == 0 || type == 0 || owner < 0 || !Finite(position) || !Finite(rotation) ||
            rotation.LengthSquared() < 0.0001f || Math.Abs(rotation.LengthSquared() - 1) > 0.01f)
            throw new ArgumentException("Invalid replicated entity state.");
        if (properties?.Count > 16) throw new ArgumentException("At most 16 properties are supported per entity.");
        var copy = new SortedDictionary<ushort, float>();
        if (properties is not null)
            foreach (var (key, value) in properties)
            {
                if (key == 0 || !float.IsFinite(value)) throw new ArgumentException("Invalid replicated property.");
                copy.Add(key, value);
            }
        Id = id; Type = type; Owner = owner; Section = section; Position = position;
        Rotation = Quaternion.Normalize(rotation);
        Properties = new ReadOnlyDictionary<ushort, float>(copy);
    }
    internal static bool Finite(Vector3 v) => float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);
    internal static bool Finite(Quaternion q) => float.IsFinite(q.X) && float.IsFinite(q.Y) && float.IsFinite(q.Z) && float.IsFinite(q.W);
}

/// <summary>Complete, bounded snapshots above transport. All methods run on the game thread.</summary>
public sealed class EntityReplicationAuthority
{
    public const int MaxEntities = 128;
    public const int MaxPayloadBytes = 32768;
    private readonly SortedDictionary<ulong, ReplicatedEntityState> _entities = new();
    private ulong _nextId = 1, _sequence;
    public ulong Session { get; }
    public EntityReplicationAuthority(ulong session)
    {
        if (session == 0) throw new ArgumentOutOfRangeException(nameof(session));
        Session = session;
    }
    public ulong Spawn(ushort type, int owner, ulong section, Vector3 position, Quaternion rotation)
    {
        if (_entities.Count >= MaxEntities || _nextId == ulong.MaxValue) return 0;
        var state = new ReplicatedEntityState(_nextId, type, owner, section, position, rotation);
        _entities.Add(state.Id, state);
        return _nextId++;
    }
    /// <summary>Server peer 0 may update any entity; clients may update only their owned entity.
    /// Applications must validate gameplay inputs before accepting client-authored poses.</summary>
    public bool Update(ulong id, int sender, Vector3 position, Quaternion rotation, IReadOnlyDictionary<ushort, float>? properties = null)
    {
        if (sender < 0 || !_entities.TryGetValue(id, out var old) || (sender != 0 && sender != old.Owner)) return false;
        _entities[id] = new ReplicatedEntityState(id, old.Type, old.Owner, old.Section, position, rotation, properties ?? old.Properties);
        return true;
    }
    public bool Despawn(ulong id) => _entities.Remove(id);
    public bool TransferOwnership(ulong id, int owner)
    {
        if (!_entities.TryGetValue(id, out var old)) return false;
        _entities[id] = new ReplicatedEntityState(id, old.Type, owner, old.Section, old.Position, old.Rotation, old.Properties);
        return true;
    }
    public void Disconnect(int peer)
    {
        if (peer <= 0) return;
        foreach (var id in _entities.Where(pair => pair.Value.Owner == peer).Select(pair => pair.Key).ToArray()) _entities.Remove(id);
    }
    public void UnloadSection(ulong section)
    {
        foreach (var id in _entities.Where(pair => pair.Value.Section == section).Select(pair => pair.Key).ToArray()) _entities.Remove(id);
    }
    public byte[] Snapshot()
    {
        if (_sequence == ulong.MaxValue) throw new InvalidOperationException("Replication sequence exhausted.");
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream);
        writer.Write(0x52554c50u); // PLUR, little endian.
        writer.Write((ushort)1);
        writer.Write((ushort)_entities.Count);
        writer.Write(Session);
        writer.Write(++_sequence);
        foreach (var state in _entities.Values)
        {
            writer.Write(state.Id); writer.Write(state.Type); writer.Write(state.Owner); writer.Write(state.Section);
            writer.Write(state.Position.X); writer.Write(state.Position.Y); writer.Write(state.Position.Z);
            writer.Write(state.Rotation.X); writer.Write(state.Rotation.Y); writer.Write(state.Rotation.Z); writer.Write(state.Rotation.W);
            writer.Write((byte)state.Properties.Count);
            foreach (var (key, value) in state.Properties) { writer.Write(key); writer.Write(value); }
        }
        return stream.ToArray();
    }
}

/// <summary>Atomic full-snapshot receiver with interpolation and stale-session rejection.</summary>
public sealed class EntityReplicationReplica
{
    private Dictionary<ulong, ReplicatedEntityState> _current = new(), _previous = new();
    private ulong _sequence;
    private double _received, _interval;
    public ulong Session { get; }
    public int ServerPeer { get; }
    public IReadOnlyDictionary<ulong, ReplicatedEntityState> Entities => new ReadOnlyDictionary<ulong, ReplicatedEntityState>(_current);
    public EntityReplicationReplica(ulong session, int serverPeer = 0)
    {
        if (session == 0 || serverPeer < 0) throw new ArgumentOutOfRangeException(nameof(session));
        Session = session; ServerPeer = serverPeer;
    }
    public bool Apply(ReadOnlySpan<byte> payload, int sender, double arrivalTime)
    {
        if (sender != ServerPeer || payload.Length > EntityReplicationAuthority.MaxPayloadBytes || payload.Length < 24 ||
            !double.IsFinite(arrivalTime) || arrivalTime < 0 || (_sequence != 0 && arrivalTime < _received)) return false;
        try
        {
            using var stream = new MemoryStream(payload.ToArray(), writable: false);
            using var reader = new BinaryReader(stream);
            if (reader.ReadUInt32() != 0x52554c50 || reader.ReadUInt16() != 1) return false;
            int count = reader.ReadUInt16();
            if (count > EntityReplicationAuthority.MaxEntities || reader.ReadUInt64() != Session) return false;
            ulong sequence = reader.ReadUInt64();
            if (sequence <= _sequence) return false;
            var next = new Dictionary<ulong, ReplicatedEntityState>(count);
            for (int i = 0; i < count; ++i)
            {
                ulong id = reader.ReadUInt64(); ushort type = reader.ReadUInt16(); int owner = reader.ReadInt32(); ulong section = reader.ReadUInt64();
                var position = new Vector3(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
                var rotation = new Quaternion(reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle(), reader.ReadSingle());
                int propertyCount = reader.ReadByte();
                if (propertyCount > 16) return false;
                var properties = new Dictionary<ushort, float>();
                for (int p = 0; p < propertyCount; ++p) properties.Add(reader.ReadUInt16(), reader.ReadSingle());
                var state = new ReplicatedEntityState(id, type, owner, section, position, rotation, properties);
                if (_current.TryGetValue(id, out var old) && old.Type != type) return false;
                next.Add(id, state);
            }
            if (stream.Position != stream.Length) return false;
            _previous = _current;
            _current = next;
            _interval = _sequence == 0 ? 0 : Math.Clamp(arrivalTime - _received, 0, 0.5);
            _received = arrivalTime;
            _sequence = sequence;
            return true;
        }
        catch (Exception error) when (error is IOException or ArgumentException) { return false; }
    }
    public bool Sample(ulong id, double now, out Vector3 position, out Quaternion rotation)
    {
        position = default; rotation = Quaternion.Identity;
        if (!double.IsFinite(now) || !_current.TryGetValue(id, out var current)) return false;
        var previous = _previous.GetValueOrDefault(id, current);
        float alpha = _interval <= 0 ? 1 : (float)Math.Clamp((now - _received) / _interval, 0, 1);
        // Double intermediates avoid overflow when finite endpoint coordinates
        // have opposite signs near float's limits.
        position = new Vector3(
            (float)((1.0 - alpha) * previous.Position.X + alpha * current.Position.X),
            (float)((1.0 - alpha) * previous.Position.Y + alpha * current.Position.Y),
            (float)((1.0 - alpha) * previous.Position.Z + alpha * current.Position.Z));
        rotation = Quaternion.Slerp(previous.Rotation, current.Rotation, alpha);
        return true;
    }
    /// <summary>Disconnect or scene replacement clears state; reconnect uses a new session object.</summary>
    public void Clear() { _current.Clear(); _previous.Clear(); }
}
