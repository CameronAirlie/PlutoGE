using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using PlutoGE.ScriptCore;

// The managed foliage API (FoliageComponent, FoliageInstance, FoliageProxyComponent) against a fake native
// ControlFoliage: the request layout, registration, the unregistered fallbacks, and that each call routes its
// operation and reads its results. The fake holds one foliage entity (42) with instances 7 and 8; promoting 7 spawns
// proxy entity 99.
unsafe class Program
{
    [StructLayout(LayoutKind.Sequential)]
    struct Request
    {
        public int Operation;
        public uint Entity;
        public ulong InstanceId;
        public Vector3 Position;
        public float Radius;
        public Quaternion Rotation;
        public Vector3 Scale;
        public int Result;
        public int TypeIndex;
        public uint LinkedEntity;
        public int Capacity;
        public int HasProxyPrefab;
        public ulong* Ids;
        public fixed byte Name[256];
    }

    const uint Foliage = 42, Proxy = 99;
    static Request last;
    static bool promoted;

    static void WriteName(Request* request, string text) =>
        Encoding.UTF8.GetBytes(text + '\0').CopyTo(new Span<byte>(request->Name, 256));

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Control(Request* request)
    {
        last = *request;
        switch (request->Operation)
        {
        case 0: // GetInstance
            if (request->Entity != Foliage) return 0;
            if (request->InstanceId == 8 || (request->InstanceId == 7 && !promoted))
            {
                request->Result = 1;
                request->TypeIndex = request->InstanceId == 7 ? 0 : 1;
                request->Position = new Vector3(1, 2, 3);
                request->Rotation = Quaternion.CreateFromYawPitchRoll(0.5f, 0, 0);
                request->Scale = new Vector3(1.5f);
                request->HasProxyPrefab = request->InstanceId == 7 ? 1 : 0;
                WriteName(request, request->InstanceId == 7 ? "Pine ñ" : "Grass");
            }
            return 1;
        case 1: // Promote
            if (request->Entity != Foliage) return 0;
            if (request->InstanceId == 7 && !promoted)
            {
                promoted = true;
                request->LinkedEntity = Proxy;
                request->Result = 1;
            }
            else
            {
                WriteName(request, "Foliage type 'Grass' has no proxy prefab.");
            }
            return 1;
        case 2: // Release
            if (request->Entity != Proxy) return 0;
            request->Result = promoted ? 1 : 0;
            promoted = false;
            return 1;
        case 3: // FindInRadius: 40 instances within any radius, more than the first buffer holds
            if (request->Entity != Foliage) return 0;
            for (var index = 0; index < Math.Min(40, request->Capacity); index++)
                request->Ids[index] = (ulong)(index + 1);
            request->Result = 40;
            return 1;
        case 4: // GetProxyLink
            if (request->Entity == Proxy && promoted)
            {
                request->LinkedEntity = Foliage;
                request->InstanceId = 7;
                request->Result = 1;
            }
            return 1;
        default:
            return 0;
        }
    }

    static void Require(bool value, string message) { if (!value) throw new Exception(message); }

    static T Create<T>(uint entity) =>
        (T)Activator.CreateInstance(typeof(T), BindingFlags.Instance | BindingFlags.NonPublic, null, new object[] { entity }, null)!;

    static void Main()
    {
        var foliage = Create<FoliageComponent>(Foliage);
        var tree = foliage.GetInstance(7);
        Require(tree.IsValid && !tree.Exists && !tree.TryPromote(out _) && foliage.FindInstances(Vector3.Zero, 5).Length == 0,
                "Unregistered fallbacks");
        Require(!default(FoliageInstance).IsValid && !foliage.GetInstance(0).IsValid, "An ID of 0 refers to no instance");

        var bridge = typeof(FoliageComponent).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var register = (delegate* unmanaged[Cdecl]<nint, int>)bridge.GetMethod("RegisterFoliageApi", BindingFlags.Public | BindingFlags.Static)!
            .MethodHandle.GetFunctionPointer();
        Require(sizeof(Request) == 344 && register(0) == 0, "ABI layout / null registration");
        Require(register((nint)(delegate* unmanaged[Cdecl]<Request*, int>)&Control) == 1, "Registration");

        Require(tree.TryGetInfo(out var info) && last.Operation == 0 && last.Entity == Foliage && last.InstanceId == 7, "Info routing");
        Require(info is { TypeIndex: 0, TypeName: "Pine ñ", HasProxyPrefab: true } && info.Position == new Vector3(1, 2, 3) &&
                info.Scale == new Vector3(1.5f) && info.Rotation == Quaternion.CreateFromYawPitchRoll(0.5f, 0, 0), "Instance info");

        var found = foliage.FindInstances(new Vector3(1, 0, 1), 10);
        Require(found.Length == 40 && found[0].Id == 1 && found[39].Id == 40 && found[0].FoliageEntityId == Foliage &&
                last.Radius == 10 && last.Position == new Vector3(1, 0, 1), "Radius query grows its buffer");
        Require(foliage.FindInstances(Vector3.Zero, float.NaN).Length == 0, "A NaN radius finds nothing");

        Require(!foliage.GetInstance(8).TryPromote(out _, out var error) && error.Contains("no proxy prefab"), "Promotion failure reason");
        Require(tree.TryPromote(out var proxy) && proxy.EntityId == Proxy && last.Operation == 1, "Promotion");
        Require(!tree.Exists, "A promoted instance still exists in the foliage");

        var link = Create<FoliageProxyComponent>(Proxy);
        Require(link.Instance == tree && link.Instance.Foliage.EntityId == Foliage, "Proxy link");
        Require(link.Release() && last.Operation == 2 && last.Entity == Proxy && tree.Exists, "Release");
        Require(!link.Instance.IsValid, "A released proxy still links an instance");

        Require(Create<FoliageComponent>(7).FindInstances(Vector3.Zero, 1).Length == 0 && !Create<FoliageComponent>(7).GetInstance(7).Exists,
                "Entity without foliage");
        Console.WriteLine("Foliage managed tests passed");
    }
}
