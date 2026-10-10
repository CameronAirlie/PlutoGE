using System.Text.Json;
using PlutoGE.ScriptCore.Authoring;
using PlutoGE.ScriptCore.Gameplay;

static class Program
{
    static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}");
    }
    static void Main()
    {
        BindingTests.Run();
        var actions = new ActionRegistry();
        int invoked = 0;
        var token = actions.Register("open", () => invoked++);
        Check(actions.Invoke("open") && invoked == 1, "Action dispatch");
        Throws<InvalidOperationException>(() => actions.Register("open", () => {}));
        token.Dispose(); token.Dispose();
        Check(!actions.Invoke("open"), "Disposed action still dispatches");

        var catalog = new LocalizationCatalog();
        catalog.LoadJson("en", "{\"hello\":\"Hello\",\"fallback\":\"Fallback\"}");
        catalog.LoadJson("fr", "{\"hello\":\"Bonjour\"}");
        catalog.Language = "fr-CA";
        Check(catalog.Get("hello") == "Bonjour" && catalog.Get("fallback") == "Fallback", "Culture fallback");
        Check(catalog.Get("unknown") == "unknown" && catalog.MissingKeys("fr").SequenceEqual(new[] { "fallback" }), "Missing translation checks");
        Throws<JsonException>(() => catalog.LoadJson("fr", "{\"hello\":\"a\",\"hello\":\"b\"}"));
        Check(catalog.Get("hello") == "Bonjour", "Failed table load mutated catalog");

        var health = new Health(100);
        int deaths = 0;
        health.Died += () => deaths++;
        health.Damage(200); health.Damage(1);
        Check(health.Current.Value == 0 && deaths == 1, "Death transition");
        health.Heal(20); health.Damage(20);
        Check(deaths == 2, "Revived health cannot die again");
        Throws<ArgumentOutOfRangeException>(() => health.Damage(float.NaN));
        var inventory = new Inventory(1);
        Check(inventory.Add("key") && !inventory.Add("coin") && inventory.Add("key", 2), "Inventory capacity");
        Check(!inventory.Remove("key", 4) && inventory.Remove("key", 3) && inventory.Add("coin"), "Inventory stacks");
        Throws<ArgumentOutOfRangeException>(() => inventory.Replace(new Dictionary<string, int> { ["bad"] = -1 }));
        Check(inventory.Count("coin") == 1, "Invalid replacement changed inventory");

        var store = new MemoryStore();
        var saves = new SaveGameService(store, 2);
        var first = new Participant("first", 10);
        var second = new Participant("second", 20);
        using var firstRegistration = saves.Register(first);
        using var secondRegistration = saves.Register(second);
        Throws<InvalidOperationException>(() => saves.Register(new Participant("first", 0)));
        saves.Save("slot", "Scenes/Main.plutoscene");
        first.Value = 30; second.Value = 40;
        var snapshot = saves.Read("slot");
        saves.Restore(snapshot);
        Check(first.Value == 10 && second.Value == 20, "Save round trip");
        first.Value = 30; second.Value = 40; second.FailOn = 20;
        Throws<AggregateException>(() => saves.Restore(snapshot));
        Check(first.Value == 30 && second.Value == 40, "Restore rollback");
        second.FailOn = null;
        var invalid = new SaveGameService.Snapshot(2, "Main", new() { ["missing"] = JsonSerializer.SerializeToElement(1) });
        Throws<InvalidDataException>(() => saves.Restore(invalid));
        Check(first.Value == 30, "Validation applied partial state");
        store.Json = JsonSerializer.Serialize(snapshot with { Version = 1 });
        saves.AddMigration(1, old => old with { Version = 2 });
        Check(saves.Read("slot").Version == 2, "Migration");
        store.Json = "{\"Version\":2,\"Scene\":\"Main\",\"Objects\":{\"first\":10,\"first\":20}}";
        Throws<JsonException>(() => saves.Read("slot"));
        store.Json = "{\"Version\":99,\"Scene\":\"Main\",\"Objects\":{}}";
        Throws<InvalidDataException>(() => saves.Read("slot"));
        var invalidState = snapshot with { Objects = new() { ["first"] = JsonSerializer.SerializeToElement(10), ["second"] = JsonSerializer.SerializeToElement("invalid") } };
        Throws<JsonException>(() => saves.Restore(invalidState));
        Check(first.Value == 30, "Participant validation mutated earlier participants");
        Throws<ArgumentException>(() => new ProjectSaveStore().Read("../escape"));
        Console.WriteLine("Authoring managed tests passed.");
    }
    sealed class MemoryStore : ISaveStore
    {
        public string Json = "";
        public string Read(string slot) => Json;
        public void Write(string slot, string json) => Json = json;
    }
    sealed class Participant(string key, int value) : ISaveParticipant
    {
        public string SaveKey => key;
        public int Value = value;
        public int? FailOn;
        public JsonElement Capture() => JsonSerializer.SerializeToElement(Value);
        public void Validate(JsonElement state) { if (state.ValueKind != JsonValueKind.Number || !state.TryGetInt32(out _)) throw new JsonException(); }
        public void Restore(JsonElement state) { Value = state.GetInt32(); if (Value == FailOn) throw new Exception("Failure after mutation"); }
    }
}
