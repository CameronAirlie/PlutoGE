using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;
using PlutoGE.ScriptCore.Authoring;

static unsafe class BindingTests
{
    private static readonly Dictionary<string, string> Attributes = [];
    private static readonly List<nint> Strings = [];
    private static string Text = "";
    private static int Writes;
    private static bool Pending;
    private static string String(nint value) => Marshal.PtrToStringUTF8(value) ?? "";
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetText(nint document, nint id, nint value) { Text = String(value); Writes++; return 1; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int SetAttribute(nint document, nint id, nint name, nint value)
    {
        Attributes[String(id) + "/" + String(name)] = String(value);
        if (String(name) == "value") Pending = true; // RmlUi can emit change on programmatic writes.
        return 1;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static nint GetAttribute(nint document, nint id, nint name)
    {
        var result = Marshal.StringToCoTaskMemUTF8(Attributes.GetValueOrDefault(String(id) + "/" + String(name), ""));
        Strings.Add(result); return result;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Subscribe(nint document, nint id, nint name) => 1;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Consume(nint document, nint id, nint name) { var result = Pending; Pending = false; return result ? 1 : 0; }
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    public static void Run()
    {
        var bridge = typeof(RmlDocument).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var register = (delegate* unmanaged[Cdecl]<nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, int>)
            bridge.GetMethod("RegisterRmlUiApi")!.MethodHandle.GetFunctionPointer();
        register(0, 0, (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, int>)&SetText, 0,
            (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, nint, int>)&SetAttribute,
            (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, nint>)&GetAttribute, 0, 0,
            (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, int>)&Subscribe,
            (nint)(delegate* unmanaged[Cdecl]<nint, nint, nint, int>)&Consume, 0, 0, 0, 0, 0, 0);
        try
        {
            using var document = new RmlDocument("UI/test.rml");
            using var binder = new RmlViewBinder();
            var text = "<unsafe>&";
            binder.BindText(document.Element("text"), () => text);
            binder.Refresh();
            Check(Text == "&lt;unsafe&gt;&amp;" && Writes == 1, "Text binding did not escape markup");
            binder.Refresh();
            Check(Writes == 1, "Unchanged text rewrote DOM");
            Attributes.Clear(); // Native hot reload recreates all attributes.
            binder.Refresh();
            Check(Writes == 2, "Hot reload did not reapply binding");
            var model = new Model();
            binder.BindProperty(document.Element("number"), model, nameof(Model.Value), twoWay: true);
            binder.Refresh();
            Check(Attributes["number/value"] == "1", "Property value not applied");
            Attributes["number/value"] = "2.5"; Pending = true;
            typeof(RmlEvent).GetMethod("DispatchRegisteredEvents", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, [1UL]);
            Check(model.Value == 2.5f, "Two-way property did not update model");
            Attributes["number/value"] = "NaN"; Pending = true;
            typeof(RmlEvent).GetMethod("DispatchRegisteredEvents", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, [2UL]);
            Check(model.Value == 2.5f && Attributes["number/value"] == "2.5", "Invalid input was committed");
            binder.Dispose();
            Attributes["number/value"] = "8"; Pending = true;
            typeof(RmlEvent).GetMethod("DispatchRegisteredEvents", BindingFlags.NonPublic | BindingFlags.Static)!.Invoke(null, [3UL]);
            Check(model.Value == 2.5f, "Disposed binding still received events");
        }
        finally
        {
            register(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
            foreach (var pointer in Strings) Marshal.FreeCoTaskMem(pointer);
            Strings.Clear();
        }
    }
    public sealed class Model { public float Value { get; set; } = 1; }
}
