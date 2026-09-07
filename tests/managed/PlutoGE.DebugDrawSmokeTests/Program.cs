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
        public int Kind;
        public Vector3 Start, End;
        public float R, G, B, A, Radius, Duration;
    }
    static Packet last;
    static string category = "", label = "";
    static int submissions, clears;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Submit(Packet* packet, byte* categoryPtr, byte* labelPtr)
    {
        last = *packet;
        category = Marshal.PtrToStringUTF8((nint)categoryPtr) ?? "";
        label = Marshal.PtrToStringUTF8((nint)labelPtr) ?? "";
        ++submissions;
        return 1;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static void Clear() { ++clears; }
    static void Require(bool value, string message) { if (!value) throw new Exception(message); }
    static void Main()
    {
        Require(!DebugDraw.Line(Vector3.Zero, Vector3.One, Vector4.One), "Unregistered API must reject safely");
        var bridge = typeof(DebugDraw).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var method = bridge.GetMethod("RegisterDebugDrawApi", BindingFlags.Public | BindingFlags.Static)!;
        var register = (delegate* unmanaged[Cdecl]<nint, nint, int>)method.MethodHandle.GetFunctionPointer();
        Require(register(0, 0) == 0, "Null registration accepted");
        Require(sizeof(Packet) == 52, "Native packet size mismatch");
        Require(register((nint)(delegate* unmanaged[Cdecl]<Packet*, byte*, byte*, int>)&Submit,
                         (nint)(delegate* unmanaged[Cdecl]<void>)&Clear) == 1, "Registration failed");
        Require(DebugDraw.Line(new(1,2,3), new(4,5,6), new(.1f,.2f,.3f,.4f), .5f, "Physics"), "Line failed");
        Require(last.Kind == 0 && last.Start == new Vector3(1,2,3) && last.End == new Vector3(4,5,6) &&
                last.A == .4f && last.Duration == .5f && category == "Physics", "Line packet corrupted");
        Require(DebugDraw.Sphere(Vector3.One, 3, Vector4.One, 2, "AI"), "Sphere failed");
        Require(last.Kind == 1 && last.Radius == 3 && last.Duration == 2, "Sphere packet corrupted");
        Require(DebugDraw.Label(Vector3.Zero, "café λ", Vector4.One, category: "方向"), "Unicode label failed");
        Require(last.Kind == 2 && label == "café λ" && category == "方向", "UTF-8 lifetime/marshalling failed");
        int before = submissions;
        Require(!DebugDraw.Label(Vector3.Zero, new string('é', 129), Vector4.One), "UTF-8 byte limit not enforced");
        Require(!DebugDraw.Label(Vector3.Zero, "a\0b", Vector4.One), "Embedded null accepted");
        Require(submissions == before, "Rejected text crossed ABI");
        DebugDraw.Clear();
        Require(clears == 1, "Clear callback failed");
        Console.WriteLine("PASS: managed debug drawing registration, packet layout, UTF-8 and callbacks");
    }
}
