using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;

unsafe class Program
{
    static float current, previous;
    static int sampledAxis;
    static bool keyDown, keyPressed, mousePressed;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int KeyDown(int key) => keyDown && key == (int)KeyCode.W ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int KeyPressed(int key) => keyPressed && key == (int)KeyCode.W ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int MousePressed(int button) => mousePressed ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Zero(int _) => 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Flag() => 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static void SetFlag(int _) { }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static Vector3 Vector() => default;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static float Axis(int packed) { sampledAxis = packed; return current; }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static float Previous(int _) => previous;
    static void Require(bool value, string message) { if (!value) throw new Exception(message); }
    static nint Export(string name) => typeof(Input).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge")!
        .GetMethod(name, BindingFlags.Public | BindingFlags.Static)!.MethodHandle.GetFunctionPointer();
    static void Main()
    {
        var register = (delegate* unmanaged[Cdecl]<nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,int>)Export("RegisterInputApi");
        nint zero = (nint)(delegate* unmanaged[Cdecl]<int,int>)&Zero;
        nint vector = (nint)(delegate* unmanaged[Cdecl]<Vector3>)&Vector;
        nint flag = (nint)(delegate* unmanaged[Cdecl]<int>)&Flag;
        Require(register((nint)(delegate* unmanaged[Cdecl]<int,int>)&KeyDown,
            (nint)(delegate* unmanaged[Cdecl]<int,int>)&KeyPressed,zero,zero,
            (nint)(delegate* unmanaged[Cdecl]<int,int>)&MousePressed,zero,vector,vector,vector,flag,flag,
            (nint)(delegate* unmanaged[Cdecl]<int,void>)&SetFlag,zero,zero,zero,
            (nint)(delegate* unmanaged[Cdecl]<int,float>)&Axis) == 1, "Input registration failed");
        var history = (delegate* unmanaged[Cdecl]<nint,int>)Export("RegisterInputHistoryApi");
        Require(history((nint)(delegate* unmanaged[Cdecl]<int,float>)&Previous)==1,"History registration failed");
        var map = new InputActionMap { Actions = [
            new() { Name="Throttle", Bindings=[new() { Kind=InputBindingKind.GamepadAxis, Axis=GamepadAxis.RightTrigger, Gamepad=2 }] },
            new() { Name="Move", Bindings=[new() { Kind=InputBindingKind.Key, Key=KeyCode.W }] },
            new() { Name="Attack", Bindings=[new() { Kind=InputBindingKind.MouseButton, MouseButton=MouseButton.Left }] }
        ] };
        Require(map.GetAxis("missing")==0 && !map.WasPressed("missing"),"Unknown action must be idle");
        Require(map.GetAxis("throttle")==0 && !map.WasPressed("Throttle"),"Released trigger must stay zero");
        current=.1f; Require(map.GetAxis("Throttle")==0 && !map.WasPressed("Throttle"),"Dead zone ignored");
        current=.7f; Require(Math.Abs(map.GetAxis("Throttle")-.7f)<.001f,"Trigger normalized twice");
        Require(sampledAxis==((2<<16)|(int)GamepadAxis.RightTrigger),"Wrong gamepad/axis dispatched");
        Require(map.WasPressed("Throttle") && map.WasPressed("Throttle"),"Press must not be consumed by reads");
        previous=current; Require(!map.WasPressed("Throttle"),"Held trigger repeated press");
        previous=.7f; current=0; Require(!map.WasPressed("Throttle"),"Release reported as press");
        previous=0; current=-.8f; Require(map.WasPressed("Throttle"),"Negative axis edge lost");
        map.Actions[0].Bindings[0].Scale=0; Require(!map.WasPressed("Throttle"),"Disabled axis fired");
        keyDown=keyPressed=true; Require(map.IsDown("MOVE") && map.WasPressed("Move"),"Keyboard binding failed");
        keyPressed=false; Require(!map.WasPressed("Move") && map.IsDown("Move"),"Held key repeated press");
        mousePressed=true; Require(map.WasPressed("Attack"),"Mouse binding failed");
        Input.ActionMap=map; Require(Input.GetAction("Move"),"Global action map failed");
        Input.ActionMap=null; Require(!Input.GetAction("Move"),"Cleared action map remained active");
        Console.WriteLine("PASS: action lookup, keyboard/mouse bindings, trigger normalization, dead zones, gamepad routing, and frame-snapshot edges");
    }
}
