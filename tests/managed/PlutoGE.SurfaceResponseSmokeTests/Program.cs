using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using PlutoGE.ScriptCore;

unsafe class Program
{
    [StructLayout(LayoutKind.Sequential)]
    struct Packet
    {
        public float Friction;
        public fixed byte Sound[4096], Particles[4096], Decal[4096];
    }
    static uint lastEntity;
    static int lastEvent, result = 1;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Resolve(uint entity, int kind, Packet* output)
    {
        lastEntity = entity; lastEvent = kind;
        *output = default;
        output->Friction = 0.8f;
        Encoding.UTF8.GetBytes("project://Piedra ñ.wav\0").CopyTo(new Span<byte>(output->Sound, 4096));
        Encoding.UTF8.GetBytes("project://Dust.plutoparticles\0").CopyTo(new Span<byte>(output->Particles, 4096));
        Encoding.UTF8.GetBytes("project://Mark.plutomaterial\0").CopyTo(new Span<byte>(output->Decal, 4096));
        return result;
    }
    static void Require(bool value, string message) { if (!value) throw new Exception(message); }
    static void Main()
    {
        var entity = (GameObject)Activator.CreateInstance(typeof(GameObject), BindingFlags.Instance | BindingFlags.NonPublic, null, new object[] { 42u }, null)!;
        Require(!SurfaceResponses.TryResolve(entity, SurfaceEvent.Footstep, out var response) && response == SurfaceResponse.Default, "Unregistered fallback");
        var bridge = typeof(SurfaceResponses).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var register = (delegate* unmanaged[Cdecl]<nint, int>)bridge.GetMethod("RegisterSurfaceResponseApi", BindingFlags.Public | BindingFlags.Static)!.MethodHandle.GetFunctionPointer();
        Require(sizeof(Packet) == 12292 && register(0) == 0, "ABI layout/registration");
        Require(register((nint)(delegate* unmanaged[Cdecl]<uint, int, Packet*, int>)&Resolve) == 1, "Registration");
        Require(SurfaceResponses.TryResolve(entity, SurfaceEvent.Impact, out response) && lastEntity == 42 && lastEvent == 1, "Query routing");
        Require(response.Friction == .8f && response.Sound == "project://Piedra ñ.wav" && response.Particles == "project://Dust.plutoparticles" && response.DecalMaterial == "project://Mark.plutomaterial", "UTF8 response");
        Require(!SurfaceResponses.TryResolve(null, SurfaceEvent.Footstep, out response) && response == SurfaceResponse.Default, "Null entity");
        Require(!SurfaceResponses.TryResolve(entity, (SurfaceEvent)5, out response), "Invalid event");
        result = 0;
        Require(!SurfaceResponses.TryResolve(entity, SurfaceEvent.Footstep, out response) && response == SurfaceResponse.Default, "Missing asset fallback");
        Console.WriteLine("Surface response managed tests passed");
    }
}
