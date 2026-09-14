using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;
using PlutoGE.ScriptCore.Networking;

internal static unsafe class ReplicatedSceneTests
{
    [StructLayout(LayoutKind.Sequential)]
    private struct StreamingRequest
    {
        public ulong Id, Generation;
        public int Operation, State;
        public float Progress;
        public nint Path, Error;
    }
    private static ulong _generation = 1;
    private static readonly List<uint> Destroyed = new();
    private static readonly Dictionary<uint, Vector3> Positions = new();
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Streaming(StreamingRequest* request) { request->Generation = _generation; return 1; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static Vector3 GetVector(uint id) => Positions.GetValueOrDefault(id);
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetVector(uint id, Vector3 value) => Positions[id] = value;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static Quaternion GetRotation(uint id) => Quaternion.Identity;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetRotation(uint id, Quaternion value) { }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int GetInt(uint id) => 1;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static void SetInt(uint id, int value) { }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static nint GetTag(uint id, int index) => 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static nint GetName(uint id) => 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static uint Find(byte* name) => 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Count(byte* tag) => 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static uint ByTag(byte* tag, int index) => 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int Destroy(uint id) { Destroyed.Add(id); Positions.Remove(id); return 1; }
    private static GameObject Object(uint id) => (GameObject)Activator.CreateInstance(typeof(GameObject),
        BindingFlags.Instance | BindingFlags.NonPublic, null, [id], null)!;
    private static void Check(bool value, string message) { if (!value) throw new Exception(message); }

    public static void Run()
    {
        var bridge = typeof(GameObject).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var streaming = (delegate* unmanaged[Cdecl]<nint, int>)bridge.GetMethod("RegisterSceneStreamingApi")!.MethodHandle.GetFunctionPointer();
        Check(streaming((nint)(delegate* unmanaged[Cdecl]<StreamingRequest*, int>)&Streaming) == 1, "Streaming registration failed");
        var register = (delegate* unmanaged[Cdecl]<nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint,
            nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, nint, int>)bridge.GetMethod("RegisterGameObjectApi")!.MethodHandle.GetFunctionPointer();
        nint get = (nint)(delegate* unmanaged[Cdecl]<uint, Vector3>)&GetVector;
        nint set = (nint)(delegate* unmanaged[Cdecl]<uint, Vector3, void>)&SetVector;
        nint integer = (nint)(delegate* unmanaged[Cdecl]<uint, int>)&GetInt;
        Check(register(get, get, set, get, set, get, set, get, get, integer,
            (nint)(delegate* unmanaged[Cdecl]<uint, int, void>)&SetInt, integer,
            (nint)(delegate* unmanaged[Cdecl]<uint, int, nint>)&GetTag,
            (nint)(delegate* unmanaged[Cdecl]<uint, int>)&Destroy,
            (nint)(delegate* unmanaged[Cdecl]<uint, nint>)&GetName,
            (nint)(delegate* unmanaged[Cdecl]<byte*, uint>)&Find,
            (nint)(delegate* unmanaged[Cdecl]<byte*, int>)&Count,
            (nint)(delegate* unmanaged[Cdecl]<byte*, int, uint>)&ByTag,
            set, get, set,
            (nint)(delegate* unmanaged[Cdecl]<uint, Quaternion>)&GetRotation,
            (nint)(delegate* unmanaged[Cdecl]<uint, Quaternion, void>)&SetRotation) == 1, "GameObject registration failed");

        var authority = new EntityReplicationAuthority(1);
        var id = authority.Spawn(1, 0, 0, Vector3.One, Quaternion.Identity);
        var replica = new EntityReplicationReplica(1);
        replica.Apply(authority.Snapshot(), 0, 0);
        var factories = new Dictionary<ushort, Func<GameObject?>> { [1] = () => Object(20) };
        using (var binding = new ReplicatedScene(replica, factories))
        {
            Check(binding.Update(0) && Positions[20] == Vector3.One, "Native pose not applied");
            authority.Despawn(id); replica.Apply(authority.Snapshot(), 0, 1);
            Check(binding.Update(1) && Destroyed.SequenceEqual(new uint[] { 20 }), "Despawn not applied exactly once");
        }
        authority.Spawn(1, 0, 0, Vector3.One, Quaternion.Identity);
        replica.Apply(authority.Snapshot(), 0, 2);
        using (var binding = new ReplicatedScene(replica, factories))
        {
            Check(binding.Update(2), "Binding setup failed");
            ++_generation;
            Check(!binding.Update(2) && replica.Entities.Count == 0 && Destroyed.Count == 1, "Scene transition destroyed a reused native ID");
        }
        replica.Apply(authority.Snapshot(), 0, 3);
        factories[1] = () => { ++_generation; return Object(30); };
        using (var binding = new ReplicatedScene(replica, factories))
            Check(!binding.Update(3) && !Positions.ContainsKey(30) && !Destroyed.Contains(30), "Factory scene transition touched a stale ID");
        replica.Apply(authority.Snapshot(), 0, 4);
        factories[1] = () => Object(40);
        using (var binding = new ReplicatedScene(replica, factories))
        {
            binding.ApplyProperties = (_, _) => { Check(!binding.Update(4), "Reentrant update accepted"); ++_generation; };
            Check(!binding.Update(4) && !Destroyed.Contains(40), "Property callback transition was missed");
        }
        replica.Apply(authority.Snapshot(), 0, 5);
        ReplicatedScene? disposedByFactory = null;
        factories[1] = () => { disposedByFactory!.Dispose(); return Object(50); };
        disposedByFactory = new ReplicatedScene(replica, factories);
        Check(!disposedByFactory.Update(5) && Destroyed.Count(value => value == 50) == 1, "Factory disposal leaked its final object");
        Console.WriteLine("Replica scene ABI: pose, despawn, generation, callback transitions and reentrancy passed.");
    }
}
