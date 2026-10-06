using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;

unsafe class Program
{
    [StructLayout(LayoutKind.Sequential)]
    struct Packet
    {
        public int Operation;
        public uint Owner, Mesh, Socket;
        public Vector3 Position, Pole;
        public Quaternion Rotation;
        public float Weight, RotationWeight;
    }
    static Packet last;
    static string name = "";
    static int result = 1, calls;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Control(Packet* r, byte* id, byte* root, byte* middle, byte* tip)
    {
        ++calls; last = *r; name = Marshal.PtrToStringUTF8((nint)id)!;
        if (r->Operation == 6) { r->Position = new(4,5,6); r->Rotation = Quaternion.Identity; }
        return result;
    }
    static T Make<T>(uint id) => (T)Activator.CreateInstance(typeof(T), BindingFlags.Instance | BindingFlags.NonPublic, null, [id], null)!;
    static void Require(bool condition) { if (!condition) throw new Exception("Skeletal ABI regression"); }
    static void Main()
    {
        var mesh = Make<GameObject>(24); var socket = Make<GameObject>(36); var animator = Make<AnimationComponent>(12);
        Require(!SkeletonAttachments.Bind(socket, mesh, "hand"));
        Require(!SkeletonAttachments.TryGetBoneWorldPose(mesh, "hand", out var p, out var q) && p == Vector3.Zero && q == Quaternion.Identity);
        var bridge = typeof(AnimationComponent).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var register = (delegate* unmanaged[Cdecl]<nint, int>)bridge.GetMethod("RegisterSkeletalApi")!.MethodHandle.GetFunctionPointer();
        Require(sizeof(Packet) == 64 && register(0) == 0);
        Require(register((nint)(delegate* unmanaged[Cdecl]<Packet*, byte*, byte*, byte*, byte*, int>)&Control) == 1);
        Require(animator.SetTwoBoneIK("支撑", mesh, "upper", "lower", "hand", new(1,2,3), new(7,8,9), .5f, Quaternion.Identity, .8f));
        Require(last.Operation == 1 && last.Owner == 12 && last.Mesh == 24 && last.Position == new Vector3(1,2,3) &&
                last.Pole == new Vector3(7,8,9) && last.Weight == .5f && last.RotationWeight == .8f && name == "支撑");
        Require(SkeletonAttachments.Bind(socket, mesh, "hand") && last.Operation == 2 && last.Socket == 36);
        Require(SkeletonAttachments.Release(socket) && last.Operation == 3);
        animator.ClearTwoBoneIK("支撑"); Require(last.Operation == 4 && name == "支撑");
        animator.ClearAllTwoBoneIK(); Require(last.Operation == 5);
        Require(SkeletonAttachments.TryGetBoneWorldPose(mesh, "hand", out p, out q) && p == new Vector3(4,5,6) && q == Quaternion.Identity);
        int before = calls; Require(!SkeletonAttachments.Bind(socket, mesh, "bad\0name") && calls == before);
        result = 0; Require(!SkeletonAttachments.Bind(socket, mesh, "hand"));
        Console.WriteLine("PASS: skeletal bridge layout, UTF-8, commands, output poses and failures");
    }
}
