using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;

unsafe class Program
{
    [StructLayout(LayoutKind.Sequential)]
    struct Packet { public uint Resolution, Cascades; public float Distance; public uint Flags; public float Foliage, GrassDensity, GrassDistance; }
    static Packet current;
    static int writes;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Get(int preset, Packet* packet)
    {
        *packet = preset < 0 ? current : new Packet { Resolution = 512, Cascades = 1, Distance = 40, Flags = 3, Foliage = 0.6f, GrassDensity = 0.4f, GrassDistance = 0.5f };
        return 1;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Set(Packet* packet) { current = *packet; writes++; return 1; }
    static void Check(bool value) { if (!value) throw new Exception("Graphics bridge contract failed"); }
    static void Main()
    {
        Check(!GraphicsSettings.QualitySupported && !GraphicsSettings.TryApplyPreset(GraphicsPreset.Low));
        var bridge = typeof(GraphicsSettings).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var method = bridge.GetMethod("RegisterGraphicsQualityApi", BindingFlags.Public | BindingFlags.Static)!;
        var register = (delegate* unmanaged[Cdecl]<nint, nint, int>)method.MethodHandle.GetFunctionPointer();
        Check(sizeof(Packet) == 28 && Marshal.OffsetOf<Packet>(nameof(Packet.Flags)).ToInt32() == 12 &&
            Marshal.OffsetOf<Packet>(nameof(Packet.GrassDistance)).ToInt32() == 24);
        Check(register(0, 0) == 0);
        Check(register((nint)(delegate* unmanaged[Cdecl]<int, Packet*, int>)&Get,
            (nint)(delegate* unmanaged[Cdecl]<Packet*, int>)&Set) == 1);
        Check(GraphicsSettings.TryGetPreset(GraphicsPreset.Low, out var low));
        Check(low.Enabled && low.CascadedShadows && !low.GlobalIllumination && low.ShadowResolution == 512);
        // Foliage and grass ceilings survive a preset roundtrip (they were once dropped by the bridge).
        Check(low.FoliageDetail == 0.6f && low.GrassDensity == 0.4f && low.GrassDistance == 0.5f && !low.ContactShadows);
        var custom = low with { Reflections = true, ShadowDistance = 55, Bloom = true, ContactShadows = true, GrassDensity = 0.25f };
        Check(GraphicsSettings.TryApplyQuality(custom));
        Check(current.Flags == 595 && current.Distance == 55 && current.Foliage == 0.6f && current.GrassDensity == 0.25f);
        Check(GraphicsSettings.TryGetQuality(out var read) && read == custom);
        int before = writes;
        try { GraphicsSettings.TryApplyQuality(custom with { ShadowDistance = float.NaN }); throw new Exception("Accepted NaN"); }
        catch (ArgumentOutOfRangeException) { }
        try { GraphicsSettings.TryApplyQuality(custom with { GrassDistance = 1.5f }); throw new Exception("Accepted grass distance above 1"); }
        catch (ArgumentOutOfRangeException) { }
        try { GraphicsSettings.TryApplyQuality(custom with { FoliageDetail = 0 }); throw new Exception("Accepted zero foliage detail"); }
        catch (ArgumentOutOfRangeException) { }
        try { GraphicsSettings.TryApplyPreset((GraphicsPreset)99); throw new Exception("Accepted invalid enum"); }
        catch (ArgumentOutOfRangeException) { }
        Check(writes == before);
        Check(GraphicsSettings.TryResetQuality() && (current.Flags & 1) == 0);
        Console.WriteLine("PASS: quality bridge layout, registration, custom roundtrip incl. foliage/grass, validation and reset");
    }
}
