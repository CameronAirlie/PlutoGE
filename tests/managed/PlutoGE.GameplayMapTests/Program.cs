using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;
using PlutoGE.ScriptCore.Gameplay;

// Component queries through a fake native bridge, plus the engine-independent map and reveal model.
unsafe class Program
{
    static readonly uint[] Colliders = Enumerable.Range(1, 100).Select(i => (uint)i).ToArray();
    static int lastKind = -1, lastInactive = -1, calls;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Find(int kind, int includeInactive, uint* ids, int capacity)
    {
        lastKind = kind; lastInactive = includeInactive; calls++;
        var source = kind == 5 ? Colliders : [];
        for (var i = 0; i < Math.Min(capacity, source.Length); i++) ids[i] = source[i];
        return source.Length;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Matrix(uint entity, float* matrix)
    {
        if (entity == 0) return 0;
        // glm column-major: scale 2 on X, translation (10, 20, 30).
        float[] values = [2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1];
        for (var i = 0; i < 16; i++) matrix[i] = values[i];
        return 1;
    }

    // Fake RmlUi bridge: records the classes each element carries.
    static readonly Dictionary<string, HashSet<string>> Classes = new();
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int SetClass(nint document, nint id, nint name, int enabled)
    {
        var element = Marshal.PtrToStringUTF8(id)!;
        if (!Classes.TryGetValue(element, out var set)) Classes[element] = set = [];
        if (enabled != 0) set.Add(Marshal.PtrToStringUTF8(name)!); else set.Remove(Marshal.PtrToStringUTF8(name)!);
        return 1;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Accept3(nint a, nint b, nint c) => 1;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Accept4(nint a, nint b, nint c, nint d) => 1;

    static readonly float[] Range = [1, 30, .1f];
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static float GetRange(uint entity, int property) => Range[property];
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static void SetRange(uint entity, int property, float value) => Range[property] = value;

    static void Require(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }
    static bool Near(float a, float b) => MathF.Abs(a - b) < .001f;

    static void Main()
    {
        var bridge = typeof(GameObject).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var register = (delegate* unmanaged[Cdecl]<nint, nint, int>)bridge.GetMethod("RegisterComponentQueryApi", BindingFlags.Public | BindingFlags.Static)!
            .MethodHandle.GetFunctionPointer();
        Require(register(0, 0) == 0, "Null registration accepted");
        Require(register((nint)(delegate* unmanaged[Cdecl]<int, int, uint*, int, int>)&Find,
            (nint)(delegate* unmanaged[Cdecl]<uint, float*, int>)&Matrix) == 1, "Component query registration failed");

        var found = GameObject.FindWithComponent<ColliderComponent>();
        Require(found.Length == 100 && found[0].EntityId == 1 && found[99].EntityId == 100, "Buffer growth lost results");
        Require(lastKind == 5 && lastInactive == 0 && calls == 2, "Wrong component kind or no retry after growth");
        GameObject.FindWithComponent<MeshComponent>(includeInactive: true);
        Require(lastKind == 0 && lastInactive == 1, "Mesh/includeInactive not forwarded");
        Require(GameObject.FindWithComponent<MeshComponent>().Length == 0, "Empty query returned objects");

        var world = found[0].WorldMatrix;
        var point = Vector3.Transform(new Vector3(1, 2, 3), world);
        Require(point == new Vector3(12, 22, 33), $"World matrix layout wrong: {point}");
        Console.WriteLine("PASS: component queries, buffer growth, inactive flag, world matrix layout");

        var json = """{ "Name": "Yard", "Image": "project://Maps/Yard.tga", "CentreX": 10, "CentreZ": -20, "WorldSize": 40, "North": "-Z" }""";
        var layout = MapLayout.Parse(json)!;
        Require(layout is { Name: "Yard", Image: "Maps/Yard.tga" }, "Layout parse failed");
        Require(layout.ImageSourceFor("UI/hud.rml") == "../Maps/Yard.tga" && layout.ImageSourceFor("project://Maps/view.rml") == "Yard.tga" &&
            layout.ImageSourceFor("UI/Menus/map.rml") == "../../Maps/Yard.tga" && layout.ImageSourceFor("root.rml") == "Maps/Yard.tga", "Document-relative image source wrong");
        Require(MapLayout.Parse(layout.ToJson()) == layout, "Layout JSON round trip failed");
        Require(MapLayout.Parse("{\"Image\":\"a.tga\",\"WorldSize\":0}") is null && MapLayout.Parse("nope") is null, "Invalid layout accepted");
        var corner = layout.Normalize(new(30, 0, -40));
        Require(Near(corner.X, 1) && Near(corner.Y, 0), "North-east is not top-right");
        Require(Vector3.Distance(layout.ToWorld(layout.Normalize(new(3, 0, -7))), new(3, 0, -7)) < .001f, "ToWorld does not invert Normalize");
        var offset = MapMath.FocusOffset(new(.25f, .75f), 200, 100);
        Require(Near(offset.X + 50, 50) && Near(offset.Y + 150, 50), "Focus offset does not centre");
        Require(Near(MapMath.LayerSize(layout, 100, 20), 200), "Layer size wrong");
        Require(Near(MapMath.Heading(-Vector3.UnitZ), 0) && Near(MapMath.Heading(Vector3.UnitX), 90) && Near(MapMath.Heading(-Vector3.UnitX), 270), "Heading wrong");
        Require(float.IsNaN(MapMath.Heading(Vector3.UnitY)), "Vertical facing produced a heading");
        Require(!new MapContact(1, "enemy", default).ShowsHeading && new MapContact(1, "friendly", default, 10).ShowsHeading, "Heading visibility wrong");
        Console.WriteLine("PASS: map layout parse/JSON/projection, focus, heading");

        var reveals = new RevealTracker(3, 1);
        reveals.Reveal(7, new(5, 0, 5), now: 10, audience: 1);
        Require(reveals.TryGet(7, 1, 11, out var heard) && heard.Opacity == 1 && heard.Position == new Vector3(5, 0, 5), "Fresh reveal hidden");
        Require(!reveals.TryGet(7, 2, 11, out _), "Reveal leaked to another audience");
        Require(reveals.TryGet(7, RevealTracker.Everyone, 11, out _), "Observer could not see reveal");
        Require(reveals.TryGet(7, 1, 12.5f, out heard) && Near(heard.Opacity, .5f), "Reveal did not fade");
        reveals.Reveal(8, new(1, 0, 1), 10, duration: 10, fade: 0);
        var visible = new List<RevealedContact>();
        reveals.Collect(visible, 1, 13.5f);
        Require(visible.Count == 1 && visible[0].Subject == 8 && reveals.Count == 1, "Expired reveal not pruned");
        reveals.Reveal(9, new(float.NaN, 0, 0), 10);
        Require(!reveals.TryGet(9, 1, 10, out _), "Invalid position revealed");
        reveals.Forget(8); reveals.Clear();
        Require(reveals.Count == 0, "Clear failed");
        Console.WriteLine("PASS: reveal audience, fade, expiry, invalid input");

        var rml = (delegate* unmanaged[Cdecl]<nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, int>)
            bridge.GetMethod("RegisterRmlUiApi", BindingFlags.Public | BindingFlags.Static)!.MethodHandle.GetFunctionPointer();
        Require(rml(0, 0, (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, int>)&Accept3, 0,
            (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, nint, int>)&Accept4, 0,
            (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, int, int>)&SetClass,
            (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, nint, int>)&Accept4, 0, 0, 0, 0, 0, 0, 0, 0) == 1, "RmlUi registration failed");
        var view = new RmlMapView(new RmlDocument("UI/hud.rml"), "minimap", RmlMapView.ArrowMarker("Icons/arrow.tga"), poolSize: 4);
        MapContact Friend(int id) => new(id, "friendly", default, 0, 1, 1);
        MapContact Foe(int id) => new(id, "enemy", default, float.NaN, 1, 2);
        string[] Marker(int i) => Classes[$"minimap-c{i}"].Where(c => c != "map-contact").OrderBy(c => c).ToArray();
        // Three heard enemies, then fewer enemies, then more teammates reuse the enemies' slots.
        Require(view.Render(layout, [Friend(1), Foe(7), Foe(8), Foe(9)], default), "Map view did not render");
        Require(view.Render(layout, [Friend(1)], default), "Map view did not render");
        view.Invalidate(); // Opening the full map invalidates cached styles.
        Require(view.Render(layout, [Friend(1), Friend(2), Friend(3)], default), "Map view did not render");
        for (var i = 1; i < 3; i++)
            Require(Marker(i).SequenceEqual(["friendly"]), $"Reused marker {i} kept stale classes: {string.Join(',', Marker(i))}");
        Require(Marker(3).SequenceEqual(["enemy", "hidden"]) || Marker(3).SequenceEqual(["hidden"]), "Unused marker is visible");
        Require(view.Render(layout, [Friend(1), Foe(7), Friend(2), Friend(3)], default) && Marker(3).SequenceEqual(["enemy"]),
            "Hidden enemy slot did not become visible again");
        Console.WriteLine("PASS: map view marker reuse never leaves stale team classes");

        var range = (delegate* unmanaged[Cdecl]<nint, nint, int>)bridge.GetMethod("RegisterSoundEmitterRangeApi", BindingFlags.Public | BindingFlags.Static)!
            .MethodHandle.GetFunctionPointer();
        Require(range((nint)(delegate* unmanaged[Cdecl]<uint, int, float>)&GetRange, (nint)(delegate* unmanaged[Cdecl]<uint, int, float, void>)&SetRange) == 1,
            "Sound emitter range registration failed");
        var emitter = found[0].GetComponent<SoundEmitterComponent>() ?? (SoundEmitterComponent)Activator.CreateInstance(typeof(SoundEmitterComponent),
            BindingFlags.NonPublic | BindingFlags.Instance, null, [1u], null)!;
        emitter.MaxDistance = 200; emitter.Rolloff = .5f; emitter.MinDistance = float.NaN;
        Require(emitter.MaxDistance == 200 && emitter.Rolloff == .5f && emitter.MinDistance == 1, "Emitter range did not round trip or accepted NaN");
        Console.WriteLine("PASS: sound emitter min/max distance and rolloff");
    }
}
