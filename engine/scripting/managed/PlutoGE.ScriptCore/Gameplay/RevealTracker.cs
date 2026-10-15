using System.Numerics;

namespace PlutoGE.ScriptCore.Gameplay;

/// <summary>A subject revealed at a fixed position, with its current fade opacity.</summary>
public readonly record struct RevealedContact(int Subject, Vector3 Position, float Opacity, float Age, int Audience);

/// <summary>
/// Time-limited reveals: "this event shows this subject to this audience for N seconds".
/// Use it for gunfire on a map, scans, pings or objective callouts. A reveal marks where the
/// event happened and does not follow the subject. A newer reveal of the same subject replaces
/// the older one. Audiences are game-defined integers such as team ids;
/// <see cref="Everyone"/> reveals to all. Callers pass their own clock, so presentation,
/// replay and tests stay deterministic. Presentation state only; never use it for authority.
/// </summary>
public sealed class RevealTracker
{
    public const int Everyone = -1;
    private readonly Dictionary<int, Entry> _entries = new();
    private readonly List<int> _expired = [];

    public RevealTracker(float defaultDuration = 3, float defaultFade = 1)
    {
        DefaultDuration = Positive(defaultDuration, 3);
        DefaultFade = Math.Clamp(float.IsFinite(defaultFade) ? defaultFade : 1, 0, DefaultDuration);
    }

    public float DefaultDuration { get; }
    public float DefaultFade { get; }
    public int Count => _entries.Count;

    /// <summary>Reveals subject at position from time now. Non-finite input is ignored.</summary>
    public void Reveal(int subject, Vector3 position, float now, int audience = Everyone, float? duration = null, float? fade = null)
    {
        if (!float.IsFinite(position.X) || !float.IsFinite(position.Y) || !float.IsFinite(position.Z) || !float.IsFinite(now)) return;
        var length = Positive(duration ?? DefaultDuration, DefaultDuration);
        _entries[subject] = new(position, now, length, Math.Clamp(float.IsFinite(fade ?? DefaultFade) ? fade ?? DefaultFade : DefaultFade, 0, length), audience);
    }

    public void Forget(int subject) => _entries.Remove(subject);
    public void Clear() => _entries.Clear();

    /// <summary>True when subject is currently revealed to viewerAudience. Expired reveals are removed.</summary>
    public bool TryGet(int subject, int viewerAudience, float now, out RevealedContact reveal)
    {
        reveal = default;
        if (!_entries.TryGetValue(subject, out var entry)) return false;
        var age = now - entry.Time;
        if (age >= entry.Duration) { _entries.Remove(subject); return false; }
        if (age < 0 || !Visible(entry.Audience, viewerAudience)) return false;
        reveal = new(subject, entry.Position, Opacity(entry, age), age, entry.Audience);
        return true;
    }

    /// <summary>Adds every reveal visible to viewerAudience and prunes expired ones.</summary>
    public void Collect(List<RevealedContact> into, int viewerAudience, float now)
    {
        _expired.Clear();
        foreach (var (subject, entry) in _entries)
        {
            var age = now - entry.Time;
            if (age >= entry.Duration) { _expired.Add(subject); continue; }
            if (age >= 0 && Visible(entry.Audience, viewerAudience))
                into.Add(new(subject, entry.Position, Opacity(entry, age), age, entry.Audience));
        }
        foreach (var subject in _expired) _entries.Remove(subject);
    }

    private static bool Visible(int audience, int viewer) => audience == Everyone || viewer == Everyone || audience == viewer;
    private static float Opacity(Entry entry, float age)
    {
        var fadeStart = entry.Duration - entry.Fade;
        return entry.Fade <= 0 || age <= fadeStart ? 1 : Math.Clamp((entry.Duration - age) / entry.Fade, 0, 1);
    }
    private static float Positive(float value, float fallback) => float.IsFinite(value) && value > 0 ? value : fallback;

    private readonly record struct Entry(Vector3 Position, float Time, float Duration, float Fade, int Audience);
}
