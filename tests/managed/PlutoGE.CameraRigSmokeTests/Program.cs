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
        public uint Camera, Target;
        public int Mode, FollowHeading;
        public Vector3 Offset, FocusOffset;
        public float Yaw, Pitch, Distance, Smoothing, Radius, Padding, Seconds, Amplitude, Frequency;
    }
    static Packet last;
    static int result = 1;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Control(Packet* request) { last = *request; return result; }
    static void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
    static GameObject Object(uint id) => (GameObject)Activator.CreateInstance(typeof(GameObject),
        BindingFlags.Instance | BindingFlags.NonPublic, null, [id], null)!;
    static void Main()
    {
        var camera = Object(12);
        var target = Object(24);
        Require(!CameraRig.Snap(camera), "Unregistered API should fail safely");
        var bridge = typeof(CameraRig).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!;
        var method = bridge.GetMethod("RegisterCameraRigApi", BindingFlags.Public | BindingFlags.Static)!;
        var register = (delegate* unmanaged[Cdecl]<nint, int>)method.MethodHandle.GetFunctionPointer();
        Require(register(0) == 0 && sizeof(Packet) == 80, "Invalid registration or packet layout");
        Require(register((nint)(delegate* unmanaged[Cdecl]<Packet*, int>)&Control) == 1, "Registration failed");
        Require(CameraRig.Configure(camera, target, new() { Mode = CameraRigMode.Orbit, Offset = new(1,2,3),
            FocusOffset = new(4,5,6), FollowHeading = false, Yaw = 30, Pitch = 20, Distance = 8,
            SmoothingSeconds = 0.3f, CollisionRadius = 0.4f, CollisionPadding = 0.1f }), "Configure failed");
        Require(last.Operation == 0 && last.Camera == 12 && last.Target == 24 && last.Mode == 1 && last.FollowHeading == 0 &&
            last.Offset == new Vector3(1,2,3) && last.FocusOffset == new Vector3(4,5,6) && last.Yaw == 30 && last.Pitch == 20 &&
            last.Distance == 8 && last.Smoothing == 0.3f && last.Radius == 0.4f && last.Padding == 0.1f, "Configuration packet corrupted");
        Require(CameraRig.Orbit(camera, 45, 25, 9) && last.Operation == 1 && last.Yaw == 45 && last.Pitch == 25 && last.Distance == 9, "Orbit packet corrupted");
        Require(CameraRig.BlendTo(camera, target, 2) && last.Operation == 2 && last.Target == 24 && last.Seconds == 2, "Blend packet corrupted");
        Require(CameraRig.Shake(camera, 0.4f, 0.5f, 10) && last.Operation == 3 && last.Amplitude == 0.4f && last.Seconds == 0.5f && last.Frequency == 10, "Shake packet corrupted");
        Require(CameraRig.Snap(camera) && last.Operation == 4, "Snap packet corrupted");
        Require(CameraRig.Configure(camera, null, new()) && last.Target == 0, "Clearing target failed");
        result = 0;
        Require(!CameraRig.Snap(camera), "Native rejection was lost");
        Console.WriteLine("PASS: camera rig ABI, configuration and runtime commands");
    }
}
