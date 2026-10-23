using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using PlutoGE.ScriptCore;

// The managed TerrainComponent against a fake native ControlTerrain: the request layout, registration, the
// unregistered fallbacks, and that each method routes its operation and reads its results.
unsafe class Program
{
    [StructLayout(LayoutKind.Sequential)]
    struct Request
    {
        public int Operation;
        public uint Entity;
        public int Layer;
        public float Radius, Opacity, Hardness;
        public Vector3 Position;
        public int Result;
        public float Height;
        public Vector3 Normal;
        public fixed float Weights[8];
        public fixed byte Name[128];
    }

    static Request last;
    static readonly string[] names = { "Dust", "Rock ñ" };

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Control(Request* request)
    {
        last = *request;
        if (request->Entity != 42) return 0;
        switch (request->Operation)
        {
        case 0: // surface: the plane y = 3 inside x in 0..10
            request->Result = request->Position.X is >= 0 and <= 10 ? 1 : 0;
            request->Height = 3.0f;
            request->Normal = Vector3.UnitY;
            break;
        case 1:
            request->Result = names.Length;
            break;
        case 2:
            if (request->Layer is < 0 or >= 2) return 1;
            var bytes = Encoding.UTF8.GetBytes(names[request->Layer] + '\0');
            bytes.CopyTo(new Span<byte>(request->Name, 128));
            request->Result = 1;
            break;
        case 3:
            request->Weights[0] = 0.25f;
            request->Weights[1] = 0.75f;
            request->Result = 1;
            break;
        case 4:
            request->Result = 1;
            break;
        case 5:
            request->Result = request->Layer == 1 && request->Radius == 2.0f ? 1 : 0;
            break;
        default:
            return 0;
        }
        return 1;
    }

    static void Require(bool value, string message) { if (!value) throw new Exception(message); }

    static void Main()
    {
        var terrain = (TerrainComponent)Activator.CreateInstance(typeof(TerrainComponent), BindingFlags.Instance | BindingFlags.NonPublic,
            null, new object[] { 42u }, null)!;
        Require(terrain.LayerCount == 0 && terrain.GetDominantLayer(Vector3.Zero) == -1 && terrain.GetLayerWeights(Vector3.Zero).Length == 0 &&
                !terrain.TryGetSurface(Vector3.Zero, out _, out var up) && up == Vector3.UnitY, "Unregistered fallbacks");

        var bridge = typeof(TerrainComponent).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var register = (delegate* unmanaged[Cdecl]<nint, int>)bridge.GetMethod("RegisterTerrainApi", BindingFlags.Public | BindingFlags.Static)!
            .MethodHandle.GetFunctionPointer();
        Require(sizeof(Request) == 216 && register(0) == 0, "ABI layout / null registration");
        Require(register((nint)(delegate* unmanaged[Cdecl]<Request*, int>)&Control) == 1, "Registration");

        Require(terrain.LayerCount == 2 && last.Operation == 1 && last.Entity == 42, "Layer count routing");
        Require(terrain.GetLayerName(1) == "Rock ñ" && last.Layer == 1, "UTF-8 layer name");
        Require(terrain.GetLayerName(5) == string.Empty, "Invalid layer name");
        Require(terrain.FindLayer("dust") == 0 && terrain.FindLayer("Snow") == -1, "Find layer by name");
        Require(terrain.TryGetSurface(new Vector3(4, 100, 2), out var height, out var normal) && height == 3.0f && normal == Vector3.UnitY &&
                last.Position == new Vector3(4, 100, 2), "Surface query");
        Require(!terrain.TryGetSurface(new Vector3(40, 0, 0), out _, out _), "Surface query off the terrain");
        var weights = terrain.GetLayerWeights(new Vector3(1, 0, 1));
        Require(weights.Length == 2 && weights[0] == 0.25f && weights[1] == 0.75f, "Layer weights");
        Require(terrain.GetDominantLayer(new Vector3(1, 0, 1)) == 1 && last.Operation == 4, "Dominant layer");
        Require(terrain.PaintLayer(new Vector3(1, 0, 1), 1, 2.0f, 0.5f, 0.25f) && last.Opacity == 0.5f && last.Hardness == 0.25f &&
                last.Operation == 5, "Paint routing");
        Require(!terrain.PaintLayer(new Vector3(1, 0, 1), 0, 2.0f), "Paint result");

        var other = (TerrainComponent)Activator.CreateInstance(typeof(TerrainComponent), BindingFlags.Instance | BindingFlags.NonPublic,
            null, new object[] { 7u }, null)!;
        Require(other.LayerCount == 0 && other.GetDominantLayer(Vector3.Zero) == -1, "Entity without a terrain");
        Console.WriteLine("Terrain managed tests passed");
    }
}
