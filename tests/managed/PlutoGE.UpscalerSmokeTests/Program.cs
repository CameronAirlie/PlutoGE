using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using PlutoGE.ScriptCore;

// The managed upscaler API against a stand-in for the native ControlUpscaler (UpscalerScriptApi.cpp): request layout,
// registration, the set/get/reset round trip, support reasons, and validation before anything reaches native code.
unsafe class Program
{
    [StructLayout(LayoutKind.Sequential)]
    struct Request
    {
        public int Operation, Technology, Quality;
        public float Sharpness;
        public int Result, Supported, Overridden;
        public fixed byte Reason[256];
    }

    // The engine's state: the project's choice, and the game's while it has one.
    static (int Technology, int Quality, float Sharpness) project = (0, 2, 0.25f);
    static (int Technology, int Quality, float Sharpness)? game;
    static int calls;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Control(Request* request)
    {
        calls++;
        switch (request->Operation)
        {
            case 0:
                var current = game ?? project;
                (request->Technology, request->Quality, request->Sharpness) = current;
                request->Overridden = game.HasValue ? 1 : 0;
                return request->Result = 1;
            case 1:
                game = (request->Technology, request->Quality, request->Sharpness);
                return request->Result = 1;
            case 2:
                game = null;
                return request->Result = 1;
            case 3:
                var dlss = request->Technology == 1;
                request->Supported = dlss ? 0 : 1;
                var reason = Encoding.UTF8.GetBytes(dlss ? "DLSS needs an NVIDIA RTX GPU" : "");
                for (var i = 0; i < reason.Length; i++) request->Reason[i] = reason[i];
                request->Reason[reason.Length] = 0;
                return request->Result = 1;
            default:
                return 0;
        }
    }

    static void Check(bool value, string what)
    {
        if (!value) throw new Exception("Upscaler bridge contract failed: " + what);
    }

    static void Main()
    {
        // Without a bridge: nothing is supported and nothing throws.
        Check(!GraphicsSettings.UpscalerSettingsSupported, "unregistered bridge reports no support");
        Check(GraphicsSettings.Upscaler == UpscalerSettings.Off, "unregistered bridge reads Off");
        Check(!GraphicsSettings.TrySetUpscaler(new UpscalerSettings(TemporalUpscaler.Fsr2)), "unregistered set fails");
        Check(!GraphicsSettings.GetUpscalerSupport(TemporalUpscaler.Dlss).Supported, "unregistered support is false");
        Check(GraphicsSettings.GetUpscalerSupport(TemporalUpscaler.None).Supported, "no upscaler is always supported");

        var bridge = typeof(GraphicsSettings).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var method = bridge.GetMethod("RegisterUpscalerApi", BindingFlags.Public | BindingFlags.Static)!;
        var register = (delegate* unmanaged[Cdecl]<nint, int>)method.MethodHandle.GetFunctionPointer();
        Check(sizeof(Request) == 284 && Marshal.OffsetOf<Request>(nameof(Request.Sharpness)).ToInt32() == 12 &&
              Marshal.OffsetOf<Request>("Reason").ToInt32() == 28, "request layout matches UpscalerScriptApi.h");
        Check(register(0) == 0, "a null entry point is refused");
        Check(register((nint)(delegate* unmanaged[Cdecl]<Request*, int>)&Control) == 1, "registration");

        // The project's upscaler until the game chooses.
        Check(GraphicsSettings.Upscaler == new UpscalerSettings(TemporalUpscaler.None, UpscalerQuality.Quality, 0.25f),
              "reads the project's upscaler");
        Check(!GraphicsSettings.IsUpscalerOverridden, "not overridden at first");

        var chosen = new UpscalerSettings(TemporalUpscaler.Fsr2, UpscalerQuality.Balanced, 0.4f);
        Check(GraphicsSettings.TrySetUpscaler(chosen), "set");
        Check(GraphicsSettings.Upscaler == chosen && GraphicsSettings.IsUpscalerOverridden, "set round trip");
        Check(GraphicsSettings.TrySetUpscaler(chosen with { Quality = UpscalerQuality.NativeAntiAliasing }) &&
              game!.Value.Quality == 4, "native anti-aliasing is the engine's DLAA value");

        var dlss = GraphicsSettings.GetUpscalerSupport(TemporalUpscaler.Dlss);
        Check(!dlss.Supported && dlss.Reason == "DLSS needs an NVIDIA RTX GPU", "support carries the reason");
        Check(GraphicsSettings.GetUpscalerSupport(TemporalUpscaler.Fsr2) == new UpscalerSupport(true, ""), "supported has no reason");

        // Invalid values throw before reaching native code.
        var before = calls;
        foreach (var invalid in new[]
                 {
                     chosen with { Sharpness = float.NaN }, chosen with { Sharpness = 1.5f }, chosen with { Sharpness = -0.1f },
                     chosen with { Technology = (TemporalUpscaler)7 }, chosen with { Quality = (UpscalerQuality)9 },
                 })
        {
            try { GraphicsSettings.TrySetUpscaler(invalid); throw new Exception($"Accepted {invalid}"); }
            catch (ArgumentOutOfRangeException) { }
        }
        try { GraphicsSettings.GetUpscalerSupport((TemporalUpscaler)7); throw new Exception("Accepted an unknown technology"); }
        catch (ArgumentOutOfRangeException) { }
        Check(calls == before, "invalid values never reach native code");

        Check(GraphicsSettings.TryResetUpscaler() && !GraphicsSettings.IsUpscalerOverridden &&
              GraphicsSettings.Upscaler.Technology == TemporalUpscaler.None, "reset returns to the project's upscaler");
        Console.WriteLine("PASS: upscaler bridge layout, registration, set/get/reset, support reasons and validation");
    }
}
