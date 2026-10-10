using System.Text.Json;

namespace PlutoGE.ScriptCore.Authoring;

public interface ISaveParticipant
{
    /// <summary>Authored stable identity; never use a transient entity ID.</summary>
    string SaveKey { get; }
    JsonElement Capture();
    void Validate(JsonElement state);
    void Restore(JsonElement state);
}

public interface ISaveStore
{
    string Read(string slot);
    void Write(string slot, string json);
}

public sealed class ProjectSaveStore : ISaveStore
{
    private static string PathFor(string slot)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(slot);
        if (slot.Length > 64 || slot.Any(c => !char.IsAsciiLetterOrDigit(c) && c != '-' && c != '_'))
            throw new ArgumentException("Slots contain only ASCII letters, digits, '-' and '_', up to 64 characters.", nameof(slot));
        return $"Saves/{slot}.json";
    }
    public string Read(string slot) => ProjectStorage.ReadUserDataText(PathFor(slot));
    public void Write(string slot, string json) => ProjectStorage.WriteUserDataText(PathFor(slot), json);
}

/// <summary>Explicitly scoped registry. Create one per game/session; scene loading remains under the application's control.</summary>
public sealed class SaveGameService(ISaveStore store, int version = 1)
{
    public const int MaximumSaveCharacters = 16 * 1024 * 1024;
    public sealed record Snapshot(int Version, string Scene, Dictionary<string, JsonElement> Objects);
    private readonly ISaveStore _store = store ?? throw new ArgumentNullException(nameof(store));
    private readonly int _version = version > 0 ? version : throw new ArgumentOutOfRangeException(nameof(version));
    private readonly Dictionary<string, ISaveParticipant> _participants = new(StringComparer.Ordinal);
    private readonly Dictionary<int, Func<Snapshot, Snapshot>> _migrations = [];
    private bool _busy;

    public IDisposable Register(ISaveParticipant participant)
    {
        ArgumentNullException.ThrowIfNull(participant);
        ArgumentException.ThrowIfNullOrWhiteSpace(participant.SaveKey);
        if (_busy) throw new InvalidOperationException("Cannot register during save/restore.");
        var key = participant.SaveKey;
        if (!_participants.TryAdd(key, participant)) throw new InvalidOperationException($"Duplicate save identity '{key}'.");
        return new ActionRegistry.Subscription(() =>
        {
            if (_busy) throw new InvalidOperationException("Cannot unregister during save/restore.");
            if (_participants.TryGetValue(key, out var current) && ReferenceEquals(current, participant)) _participants.Remove(key);
        });
    }

    public void AddMigration(int fromVersion, Func<Snapshot, Snapshot> migrate)
    {
        ArgumentNullException.ThrowIfNull(migrate);
        if (fromVersion < 1 || fromVersion >= _version) throw new ArgumentOutOfRangeException(nameof(fromVersion));
        _migrations.Add(fromVersion, migrate);
    }

    public void Save(string slot, string scene)
    {
        Enter();
        try
        {
            var states = new Dictionary<string, JsonElement>(StringComparer.Ordinal);
            foreach (var (key, participant) in _participants) states.Add(key, participant.Capture().Clone());
            var snapshot = new Snapshot(_version, scene, states);
            ValidateSnapshot(snapshot);
            var json = JsonSerializer.Serialize(snapshot);
            if (json.Length > MaximumSaveCharacters) throw new InvalidDataException("Save exceeds the size limit.");
            _store.Write(slot, json);
        }
        finally { _busy = false; }
    }

    /// <summary>Read first, load Snapshot.Scene, register its participants, then call Restore.</summary>
    public Snapshot Read(string slot)
    {
        var json = _store.Read(slot);
        if (json.Length > MaximumSaveCharacters) throw new InvalidDataException("Save exceeds the size limit.");
        using var document = JsonDocument.Parse(json);
        RejectDuplicateProperties(document.RootElement);
        var snapshot = document.RootElement.Deserialize<Snapshot>() ?? throw new JsonException("Empty save.");
        if (snapshot.Version < 1 || snapshot.Version > _version) throw new InvalidDataException("Unsupported save version.");
        while (snapshot.Version < _version)
        {
            if (!_migrations.TryGetValue(snapshot.Version, out var migrate)) throw new InvalidDataException($"Missing migration from version {snapshot.Version}.");
            var next = migrate(snapshot) ?? throw new InvalidDataException("Migration returned no snapshot.");
            if (next.Version != snapshot.Version + 1) throw new InvalidDataException("Migrations must advance exactly one version.");
            snapshot = next;
        }
        ValidateSnapshot(snapshot);
        return snapshot;
    }

    public void Restore(Snapshot snapshot, bool requireAllParticipants = true)
    {
        ValidateSnapshot(snapshot);
        Enter();
        try
        {
            var changes = new List<(ISaveParticipant Target, JsonElement State, JsonElement Before)>();
            foreach (var (key, state) in snapshot.Objects)
            {
                if (!_participants.TryGetValue(key, out var target))
                {
                    if (requireAllParticipants) throw new InvalidDataException($"Missing save participant '{key}'.");
                    continue;
                }
                target.Validate(state);
                changes.Add((target, state, target.Capture().Clone()));
            }
            var applied = 0;
            try
            {
                for (; applied < changes.Count; ++applied) changes[applied].Target.Restore(changes[applied].State);
            }
            catch (Exception failure)
            {
                var errors = new List<Exception> { failure };
                for (var i = Math.Min(applied, changes.Count - 1); i >= 0; --i)
                    try { changes[i].Target.Restore(changes[i].Before); } catch (Exception rollback) { errors.Add(rollback); }
                throw new AggregateException("Restore failed; attempted rollback.", errors);
            }
        }
        finally { _busy = false; }
    }

    private void Enter()
    {
        if (_busy) throw new InvalidOperationException("Reentrant save/restore is not supported.");
        _busy = true;
    }
    private void ValidateSnapshot(Snapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (snapshot.Version != _version || snapshot.Scene is null || snapshot.Objects is null || snapshot.Objects.Count > 4096 ||
            snapshot.Objects.Keys.Any(key => string.IsNullOrWhiteSpace(key) || key.Length > 256) ||
            snapshot.Objects.Values.Any(state => state.ValueKind == JsonValueKind.Undefined))
            throw new InvalidDataException("Invalid save snapshot.");
    }
    private static void RejectDuplicateProperties(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in element.EnumerateObject())
            {
                if (!names.Add(property.Name)) throw new JsonException($"Duplicate save property '{property.Name}'.");
                RejectDuplicateProperties(property.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
            foreach (var item in element.EnumerateArray()) RejectDuplicateProperties(item);
    }
}
