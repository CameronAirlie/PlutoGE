namespace PlutoGE.ScriptCore.Gameplay;

/// <summary>Bounded item stacks. The project supplies item definitions and presentation.</summary>
public sealed class Inventory(int capacity = 20)
{
    private readonly int _capacity = capacity > 0 ? capacity : throw new ArgumentOutOfRangeException(nameof(capacity));
    private readonly Dictionary<string, int> _items = new(StringComparer.Ordinal);
    public event Action? Changed;
    public IReadOnlyDictionary<string, int> Items => new System.Collections.ObjectModel.ReadOnlyDictionary<string, int>(_items);
    public int Count(string item) => _items.GetValueOrDefault(item);
    public bool Add(string item, int amount = 1)
    {
        Validate(item, amount);
        var count = Count(item);
        if ((count == 0 && _items.Count >= _capacity) || count > int.MaxValue - amount) return false;
        _items[item] = count + amount;
        Changed?.Invoke();
        return true;
    }
    public bool Remove(string item, int amount = 1)
    {
        Validate(item, amount);
        var count = Count(item);
        if (count < amount) return false;
        if (count == amount) _items.Remove(item); else _items[item] = count - amount;
        Changed?.Invoke();
        return true;
    }
    public void Replace(IReadOnlyDictionary<string, int> items)
    {
        ArgumentNullException.ThrowIfNull(items);
        if (items.Count > _capacity) throw new ArgumentException("Inventory exceeds capacity.", nameof(items));
        var copy = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var (item, amount) in items) { Validate(item, amount); copy.Add(item, amount); }
        _items.Clear();
        foreach (var pair in copy) _items.Add(pair.Key, pair.Value);
        Changed?.Invoke();
    }
    private static void Validate(string item, int amount)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(item);
        if (amount <= 0) throw new ArgumentOutOfRangeException(nameof(amount));
    }
}
