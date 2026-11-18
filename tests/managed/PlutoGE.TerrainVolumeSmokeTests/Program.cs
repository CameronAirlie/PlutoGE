using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Net;
using System.Net.Sockets;
using PlutoGE.ScriptCore;
using PlutoGE.ScriptCore.Networking;

// The managed TerrainVolumes API and TerrainVolumeComponent against a fake native ControlTerrainVolume: the request
// layout, registration, the unregistered fallbacks, routing of shapes, modes and budgets, results, and the byte
// buffers of save games.
unsafe class Program
{
    [StructLayout(LayoutKind.Sequential)]
    struct Request
    {
        public int Operation;
        public uint Entity;
        public int Shape, Mode, Material, Result;
        public float Radius, MaxExtent;
        public Vector3 A, B, HalfExtents;
        public Quaternion Rotation;
        public float RockRemoved, RockAdded;
        public int MainMaterial, VolumesChanged, FoliageRemoved, DebrisPieces, DebrisSpawned;
        public ulong Buffer;
        public long BufferSize;
    }

    static Request last;
    static readonly byte[] capture = { (byte)'P', (byte)'L', (byte)'V', (byte)'A', 1, 2, 3 };
    static byte[] restored = [];

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Control(Request* request)
    {
        last = *request;
        if (request->Entity != 0 && request->Entity != 42) return 0;
        switch (request->Operation)
        {
        case 0: // edit: refuse anything wider than the budget, dig 1.5 m³ of layer 2
            var reach = request->Shape == 2 ? request->HalfExtents.X : request->Radius;
            if (request->MaxExtent < 0 && reach * 2 > 32) { request->Result = -1; return 1; }
            request->Result = 1;
            request->RockRemoved = request->Mode == 0 ? 1.5f : 0;
            request->RockAdded = request->Mode == 1 ? 2.5f : 0;
            request->MainMaterial = request->Mode == 0 ? 2 : -1;
            request->VolumesChanged = request->Entity == 0 ? 2 : 1;
            request->FoliageRemoved = request->Entity == 0 ? 3 : 0;
            request->DebrisSpawned = request->DebrisPieces > 0 ? request->DebrisPieces - 1 : 0;
            for (var piece = 0; piece < request->DebrisSpawned && (piece + 1) * 4 <= request->BufferSize; ++piece)
                ((uint*)request->Buffer)[piece] = 500u + (uint)piece;
            return 1;
        case 1: request->Result = 1; return 1;
        case 2: request->Result = 0; return 1;
        case 3: request->Result = 1; return 1;
        case 4: // capture: the size, then the copy
            request->Result = capture.Length;
            if (request->BufferSize >= capture.Length)
                capture.CopyTo(new Span<byte>((void*)request->Buffer, capture.Length));
            return 1;
        case 5:
            restored = new Span<byte>((void*)request->Buffer, (int)request->BufferSize).ToArray();
            request->Result = restored.Length > 4 ? 1 : 0;
            return 1;
        case 6: request->Result = 7; return 1;
        case 7: request->Result = 1; return 1;
        default: return 0;
        }
    }

    static void Require(bool value, string message) { if (!value) throw new Exception(message); }


    // Server and client over loopback: a joining client gets the state, a client's request is applied by the server
    // (when it accepts it) and comes back to every client as an applied edit.
    static void Replication()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var port = ((IPEndPoint)listener.LocalEndpoint).Port;
        listener.Stop();
        using var server = new NetworkServer();
        using var serverReplicator = new TerrainVolumeServerReplicator(server);
        var accepted = 0;
        serverReplicator.Accept = (_, edit) => edit.Shape.Radius < 5;
        serverReplicator.Applied += (_, _, result) => accepted += result.Changed ? 1 : 0;
        server.Start((ushort)port, bindAddress: "127.0.0.1");
        using var client = new NetworkClient();
        using var clientReplicator = new TerrainVolumeClientReplicator(client);
        var states = 0;
        var applied = new List<TerrainVolumeEdit>();
        clientReplicator.StateReceived += () => ++states;
        clientReplicator.Applied += (edit, _) => applied.Add(edit);
        client.ConnectAsync("127.0.0.1", (ushort)port).GetAwaiter().GetResult();
        void Pump(Func<bool> done)
        {
            for (var tick = 0; tick < 400 && !done(); ++tick)
            {
                server.Poll();
                client.Poll();
                Thread.Sleep(5);
            }
        }
        Pump(() => states > 0);
        Require(states == 1 && restored.SequenceEqual(capture), "A joining client takes the server's state");

        var dig = new TerrainVolumeEdit(TerrainVolumeShape.Sphere(new Vector3(1, 2, 3), 1.5f), TerrainVolumeEditMode.Dig, DebrisPieces: 3);
        Require(clientReplicator.Request(dig), "The client asks");
        Pump(() => applied.Count > 0);
        Require(accepted == 1 && applied.Count == 1 && applied[0] == dig, "The server applies the request and every client follows");
        Require(last.DebrisPieces == 3, "Clients make their own debris");
        clientReplicator.LocalDebris = false;
        serverReplicator.Apply(dig with { Shape = TerrainVolumeShape.Sphere(Vector3.Zero, 1) });
        Pump(() => applied.Count > 1);
        Require(applied.Count == 2 && last.DebrisPieces == 0, "The server's own edits reach clients; debris can stay off");
        clientReplicator.Request(dig with { Shape = TerrainVolumeShape.Sphere(Vector3.Zero, 9) });
        Pump(() => false);
        Require(applied.Count == 2 && accepted == 2, "A request the server refuses goes nowhere");
    }

    static TerrainVolumeComponent Component(uint entity) =>
        (TerrainVolumeComponent)Activator.CreateInstance(typeof(TerrainVolumeComponent), BindingFlags.Instance | BindingFlags.NonPublic,
            null, new object[] { entity }, null)!;

    static void Main()
    {
        var volume = Component(42);
        Require(!TerrainVolumes.IsSupported && !TerrainVolumes.Dig(Vector3.Zero, 1).Changed && TerrainVolumes.CaptureEdits().Length == 0 &&
                !TerrainVolumes.RestoreEdits(capture) && volume.IsUpToDate && volume.ChunkCount == 0 && !volume.Save(),
                "Unregistered fallbacks");

        var bridge = typeof(TerrainVolumes).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var register = (delegate* unmanaged[Cdecl]<nint, int>)bridge.GetMethod("RegisterTerrainVolumeApi", BindingFlags.Public | BindingFlags.Static)!
            .MethodHandle.GetFunctionPointer();
        Require(sizeof(Request) == 128 && register(0) == 0, "ABI layout / null registration");
        Require(register((nint)(delegate* unmanaged[Cdecl]<Request*, int>)&Control) == 1 && TerrainVolumes.IsSupported, "Registration");

        var dig = TerrainVolumes.Dig(new Vector3(1, 2, 3), 1.25f);
        Require(dig.Changed && !dig.Refused && dig.RockRemoved == 1.5f && dig.MainLayer == 2 && dig.TerrainsChanged == 2 && dig.FoliageRemoved == 3,
                "Dig results");
        Require(last.Operation == 0 && last.Entity == 0 && last.Shape == 0 && last.Mode == 0 && last.A == new Vector3(1, 2, 3) &&
                last.Radius == 1.25f && last.MaxExtent < 0 && last.Rotation == Quaternion.Identity, "Dig routing");

        var debris = TerrainVolumes.Dig(new Vector3(1, 2, 3), 1.25f, debrisPieces: 5);
        Require(last.DebrisPieces == 5 && debris.DebrisSpawned == 4 && debris.Debris.Length == 4 && debris.Debris[3].EntityId == 503,
                "Debris routing, and the pieces come back");

        // Edits as data: they round-trip through bytes, and unknown or newer bytes are refused.
        var turned = Quaternion.CreateFromAxisAngle(Vector3.UnitX, 0.25f);
        var asData = new TerrainVolumeEdit(TerrainVolumeShape.Box(new Vector3(1, 2, 3), new Vector3(4, 5, 6), turned), TerrainVolumeEditMode.Fill, 3, 12f, 2);
        Require(TerrainVolumeEdit.TryRead(asData.ToBytes(), out var back) && back == asData, "An edit round-trips through bytes");
        var capsule = new TerrainVolumeEdit(TerrainVolumeShape.Capsule(Vector3.Zero, Vector3.UnitZ, 2), TerrainVolumeEditMode.Dig);
        Require(TerrainVolumeEdit.TryRead(capsule.ToBytes(), out var capsuleBack) && capsuleBack.MaxExtent is null && capsuleBack.Shape.End == Vector3.UnitZ,
                "No budget stays no budget");
        var newer = asData.ToBytes(); newer[0] = 9;
        var badShape = asData.ToBytes(); badShape[1] = 7;
        Require(!TerrainVolumeEdit.TryRead(newer, out _) && !TerrainVolumeEdit.TryRead(badShape, out _) && !TerrainVolumeEdit.TryRead(new byte[3], out _),
                "Damaged edits are refused");
        var applied = TerrainVolumes.Apply(asData);
        Require(applied.Changed && last.Shape == 2 && last.Mode == 1 && last.Material == 3 && last.MaxExtent == 12f && last.DebrisPieces == 2,
                "Applying an edit as data routes all of it");

        var fill = TerrainVolumes.Fill(Vector3.Zero, 2, 4);
        Require(fill.RockAdded == 2.5f && fill.MainLayer == -1 && last.Mode == 1 && last.Material == 4, "Fill routing");
        TerrainVolumes.Tunnel(new Vector3(0, 0, 0), new Vector3(0, 0, 5), 1.5f);
        Require(last.Shape == 1 && last.B == new Vector3(0, 0, 5) && last.Radius == 1.5f, "Tunnel routing");
        TerrainVolumes.Paint(Vector3.One, 3, 5);
        Require(last.Mode == 2 && last.Material == 5, "Paint routing");
        var turn = Quaternion.CreateFromAxisAngle(Vector3.UnitY, 0.5f);
        TerrainVolumes.Edit(TerrainVolumeShape.Box(Vector3.One, new Vector3(1, 2, 3), turn), TerrainVolumeEditMode.Dig);
        Require(last.Shape == 2 && last.HalfExtents == new Vector3(1, 2, 3) && last.Rotation == turn, "Box routing");

        Require(TerrainVolumes.Dig(Vector3.Zero, 20).Refused, "Budget refusal");
        Require(TerrainVolumes.Edit(TerrainVolumeShape.Sphere(Vector3.Zero, 20), TerrainVolumeEditMode.Dig, maxExtent: 0).Changed &&
                last.MaxExtent == 0, "Budget override");

        TerrainVolumes.Flush();
        Require(last.Operation == 1 && last.Entity == 0, "Flush routing");
        var bytes = TerrainVolumes.CaptureEdits();
        Require(bytes.SequenceEqual(capture) && last.Operation == 4 && last.BufferSize == capture.Length, "Capture copies the edits");
        Require(TerrainVolumes.RestoreEdits(bytes) && restored.SequenceEqual(capture) && last.Operation == 5, "Restore passes the edits");
        Require(!TerrainVolumes.RestoreEdits([]) && !TerrainVolumes.RestoreEdits([1, 2]), "Empty and refused restores");
        TerrainVolumes.ClearEdits();
        Require(last.Operation == 7, "Clear routing");

        var mine = volume.Edit(TerrainVolumeShape.Sphere(Vector3.Zero, 1), TerrainVolumeEditMode.Dig);
        Require(mine.Changed && mine.TerrainsChanged == 1 && last.Entity == 42, "Component edit routing");
        Require(!volume.IsUpToDate && last.Operation == 2, "Up to date routing");
        Require(volume.ChunkCount == 7 && volume.Save() && last.Operation == 3, "Chunk count and save");
        volume.Flush();
        Require(last.Operation == 1 && last.Entity == 42, "Component flush routing");

        var other = Component(7);
        Require(!other.Edit(TerrainVolumeShape.Sphere(Vector3.Zero, 1), TerrainVolumeEditMode.Dig).Changed && other.ChunkCount == 0,
                "Entity without a terrain volume");
        Replication();
        Console.WriteLine("Terrain volume managed tests passed");
    }
}
