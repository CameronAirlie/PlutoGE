namespace PlutoGE.ScriptCore.Gameplay;

public sealed class Health
{
    public float Maximum { get; }
    private readonly ObservableValue<float> _current;
    public IReadOnlyObservable<float> Current => _current;
    public event Action? Died;
    public Health(float maximum = 100)
    {
        if (!float.IsFinite(maximum) || maximum <= 0) throw new ArgumentOutOfRangeException(nameof(maximum));
        Maximum = maximum;
        _current = new(maximum);
    }
    public void Set(float value, bool notifyDeath = true)
    {
        if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
        var wasAlive = Current.Value > 0;
        _current.Value = Math.Clamp(value, 0, Maximum);
        if (notifyDeath && wasAlive && Current.Value == 0) Died?.Invoke();
    }
    public void Damage(float amount) { ValidateAmount(amount); Set(Current.Value - amount); }
    public void Heal(float amount) { ValidateAmount(amount); Set(Math.Min(Maximum, Current.Value + amount)); }
    private static void ValidateAmount(float amount)
    {
        if (!float.IsFinite(amount) || amount < 0) throw new ArgumentOutOfRangeException(nameof(amount));
    }
}
