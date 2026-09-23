using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;

unsafe class Program
{
    static string? clip;
    static int capacity, preloadResult = 1, scopeResult;
    static readonly List<int> ended = [];
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Preload(nint path) { clip = Marshal.PtrToStringUTF8(path); return preloadResult; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static void Prewarm(int count) { capacity = count; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Begin(nint name) { return scopeResult++; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static void End(int token) { ended.Add(token); }
    static void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
    static delegate* unmanaged[Cdecl]<nint, nint, int> Registration(string name)
    {
        var bridge = typeof(Audio).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        return (delegate* unmanaged[Cdecl]<nint, nint, int>)bridge.GetMethod(name, BindingFlags.Public | BindingFlags.Static)!.MethodHandle.GetFunctionPointer();
    }
    static void Main()
    {
        Require(!Audio.PreloadClip("missing.wav"), "Unregistered audio must fail safely");
        Audio.PrewarmVoices(0);
        using (Profiler.Scope("Unregistered trace")) {}
        var register = Registration("RegisterAudioPreparationApi");
        Require(register(0, 0) == 0, "Null registration accepted");
        Require(register((nint)(delegate* unmanaged[Cdecl]<nint, int>)&Preload,
            (nint)(delegate* unmanaged[Cdecl]<int, void>)&Prewarm) == 1, "Audio registration failed");
        const string reference = "project://Audio/écho.wav";
        Require(Audio.PreloadClip(reference) && clip == reference, "UTF-8 audio reference corrupted");
        preloadResult = 0;
        Require(!Audio.PreloadClip(reference), "Preload failure was hidden");
        Audio.PrewarmVoices(19);
        Require(capacity == 19, "Voice capacity corrupted");
        try { Audio.PrewarmVoices(-1); throw new Exception("Negative capacity accepted"); }
        catch (ArgumentOutOfRangeException) {}
        try { Audio.PreloadClip(" "); throw new Exception("Blank clip accepted"); }
        catch (ArgumentException) {}
        register = Registration("RegisterProfilingApi");
        Require(register(0, 0) == 0, "Null profiling registration accepted");
        Require(register((nint)(delegate* unmanaged[Cdecl]<nint, int>)&Begin,
            (nint)(delegate* unmanaged[Cdecl]<int, void>)&End) == 1, "Profiler registration failed");
        Profiler.CpuScope empty = default;
        empty.Dispose();
        Require(ended.Count == 0, "Default scope ended an unrelated trace");
        try
        {
            using var outer = Profiler.Scope("Outer");
            using var inner = Profiler.Scope("Inner");
            throw new InvalidOperationException();
        }
        catch (InvalidOperationException) {}
        Require(ended.SequenceEqual(new[] { 1, 0 }), "Nested scopes did not unwind in reverse order");
        var scope = Profiler.Scope("Manual disposal");
        scope.Dispose(); scope.Dispose();
        Require(ended.Count == 3, "Scope ended twice");
        scopeResult = -1;
        using (Profiler.Scope("Disabled trace")) {}
        Require(ended.Count == 3, "Disabled trace was ended");
        Console.WriteLine("PASS: audio preparation ABI, validation, native failure propagation and profiler scope lifetime");
    }
}
