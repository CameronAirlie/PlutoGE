using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;

unsafe class Program
{
    static int vibrationPad, vibrationCalls;
    static float vibrationLow, vibrationHigh, vibrationSeconds;
    static bool vibrationAvailable = true;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Vibration(int pad, float low, float high, float seconds)
    {
        vibrationCalls++; vibrationPad = pad; vibrationLow = low; vibrationHigh = high; vibrationSeconds = seconds;
        return vibrationAvailable ? 1 : 0;
    }
    static float current, previous;
    static int sampledAxis;
    static bool keyDown, keyPressed, mousePressed, keyReleased, padDown, padPressed;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int KeyReleased(int key) => keyReleased && key == (int)KeyCode.W ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int PadDown(int packed) => padDown && (packed & 0xffff) == (int)GamepadButton.B ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int PadPressed(int packed) => padPressed && (packed & 0xffff) == (int)GamepadButton.B ? 1 : 0;
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
    static int activeDevice = 1;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    static int Device() => activeDevice;
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
        Require(!Input.TrySetGamepadVibration(.5f, .2f, .1f), "Absent output backend should be unavailable");
        var vibration = (delegate* unmanaged[Cdecl]<nint,int>)Export("RegisterGamepadVibrationApi");
        Require(vibration((nint)(delegate* unmanaged[Cdecl]<int,float,float,float,int>)&Vibration) == 1, "Vibration registration failed");
        Require(Input.TrySetGamepadVibration(.7f, .2f, .3f, 2) && vibrationPad == 2 &&
            vibrationLow == .7f && vibrationHigh == .2f && vibrationSeconds == .3f, "Motor values or controller routing changed");
        int calls = vibrationCalls;
        Require(!Input.TrySetGamepadVibration(float.NaN, 0, 1) && !Input.TrySetGamepadVibration(0, float.PositiveInfinity, 1) &&
            !Input.TrySetGamepadVibration(0, 0, -1) && !Input.TrySetGamepadVibration(1, 1, 6) &&
            !Input.TrySetGamepadVibration(-1, 0, 1) && !Input.TrySetGamepadVibration(0, 2, 1) &&
            !Input.TrySetGamepadVibration(1, 1, 1, -1) && vibrationCalls == calls, "Invalid vibration reached native backend");
        Require(Input.StopGamepadVibration(2) && vibrationPad == 2 && vibrationLow == 0 && vibrationHigh == 0 && vibrationSeconds == 0,
            "Stop did not zero both motors on selected controller");
        vibrationAvailable = false;
        Require(!Input.TrySetGamepadVibration(1, 1, .1f), "Unsupported output incorrectly reported success");
        Console.WriteLine("PASS: vibration bridge registration, motor/controller routing, argument validation, stop and unavailable backend");
        var register = (delegate* unmanaged[Cdecl]<nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,nint,int>)Export("RegisterInputApi");
        nint zero = (nint)(delegate* unmanaged[Cdecl]<int,int>)&Zero;
        nint vector = (nint)(delegate* unmanaged[Cdecl]<Vector3>)&Vector;
        nint flag = (nint)(delegate* unmanaged[Cdecl]<int>)&Flag;
        Require(register((nint)(delegate* unmanaged[Cdecl]<int,int>)&KeyDown,
            (nint)(delegate* unmanaged[Cdecl]<int,int>)&KeyPressed,(nint)(delegate* unmanaged[Cdecl]<int,int>)&KeyReleased,zero,
            (nint)(delegate* unmanaged[Cdecl]<int,int>)&MousePressed,zero,vector,vector,vector,flag,flag,
            (nint)(delegate* unmanaged[Cdecl]<int,void>)&SetFlag,zero,(nint)(delegate* unmanaged[Cdecl]<int,int>)&PadDown,
            (nint)(delegate* unmanaged[Cdecl]<int,int>)&PadPressed,
            (nint)(delegate* unmanaged[Cdecl]<int,float>)&Axis) == 1, "Input registration failed");
        var history = (delegate* unmanaged[Cdecl]<nint,nint,nint,int>)Export("RegisterInputHistoryApi");
        Require(history((nint)(delegate* unmanaged[Cdecl]<int,float>)&Previous, (nint)(delegate* unmanaged[Cdecl]<int>)&Device, flag)==1,"History registration failed");
        Require(Input.UsingController && Input.ActiveGamepad == 0, "Active device bridge failed");
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
        var prompts = new InputActionMap { Actions = [new() { Name = "Interact", Bindings = [
            new() { Kind = InputBindingKind.Key, Key = KeyCode.E },
            new() { Kind = InputBindingKind.GamepadButton, Button = GamepadButton.X },
            new() { Kind = InputBindingKind.MouseButton, MouseButton = MouseButton.Left, Scale = 0 }
        ] }] };
        Require(InputPrompts.Binding(prompts, "Interact", InputDevice.KeyboardMouse) == "E", "KBM prompt used controller or disabled binding");
        Require(InputPrompts.Binding(prompts, "Interact") == "X", "Active controller prompt incorrect");
        activeDevice = 0;
        Require(InputPrompts.Format(prompts, "HOLD {Interact}") == "HOLD E", "Device switch left stale prompt");
        prompts.Actions[0].Bindings[0].Key = KeyCode.F;
        Require(InputPrompts.Binding(prompts, "Interact") == "F", "Remap left stale prompt");
        prompts.Actions[0].Bindings.RemoveAt(0);
        Require(InputPrompts.Binding(prompts, "Interact") == "UNBOUND", "Missing KBM binding fell back to controller");
        Require(InputPrompts.Binding(prompts, "Missing") == "UNBOUND", "Missing action invented a binding");
        activeDevice = 1;
        Console.WriteLine("PASS: action lookup, keyboard/mouse bindings, trigger normalization, dead zones, gamepad routing, and frame-snapshot edges");
        Triggers();
    }

    // Frames are simulated by setting the control state and calling Update; a W tap rolls while a W hold
    // walks, and on B a double tap rolls while an exclusive single tap crouches.
    static void Triggers()
    {
        keyDown = keyPressed = keyReleased = padDown = padPressed = false;
        var map = new InputActionMap { Actions = [
            new() { Name = "Roll", Bindings = [
                new() { Kind = InputBindingKind.Key, Key = KeyCode.W, Trigger = new() { Kind = InputTriggerKind.Tap, Duration = .2f } },
                new() { Kind = InputBindingKind.GamepadButton, Button = GamepadButton.B,
                        Trigger = new() { Kind = InputTriggerKind.MultiTap, Count = 2, Window = .25f } }] },
            new() { Name = "Walk", Bindings = [
                new() { Kind = InputBindingKind.Key, Key = KeyCode.W, Trigger = new() { Kind = InputTriggerKind.Hold, Duration = .2f } }] },
            new() { Name = "Crouch", Bindings = [
                new() { Kind = InputBindingKind.GamepadButton, Button = GamepadButton.B,
                        Trigger = new() { Kind = InputTriggerKind.MultiTap, Count = 1, Window = .25f, Exclusive = true } }] }
        ] };
        void Key(bool down, bool pressed = false, bool released = false) { keyDown = down; keyPressed = pressed; keyReleased = released; map.Update(.05f); }
        void Pad(bool down, bool pressed = false, float dt = .05f) { padDown = down; padPressed = pressed; map.Update(dt); }

        keyDown = keyPressed = true;
        Require(!map.WasPressed("Roll") && !map.IsDown("Walk"), "A never-updated map fired a stateful trigger");

        Key(true, pressed: true); Require(!map.WasPressed("Roll") && !map.IsDown("Walk"), "Tap or hold fired on press");
        Key(true); Key(false, released: true);
        Require(map.WasPressed("Roll") && map.IsDown("Roll") && !map.WasPressed("Walk"), "Short press did not tap");
        Key(false); Require(!map.WasPressed("Roll") && !map.IsDown("Roll"), "Tap repeated");

        Key(true, pressed: true);
        int walkStarts = 0;
        for (var frame = 0; frame < 6; frame++) { Key(true); if (map.WasPressed("Walk")) walkStarts++; }
        Require(walkStarts == 1 && map.IsDown("Walk") && Math.Abs(map.GetAxis("Walk") - 1) < .001f, "Hold did not start once and stay active");
        Key(false, released: true); Require(!map.WasPressed("Roll") && !map.IsDown("Walk"), "Long hold tapped or kept walking");

        Key(false, pressed: true, released: true); Require(map.WasPressed("Roll"), "Press and release inside one frame lost");
        Key(false);

        Pad(true, pressed: true); Require(!map.WasPressed("Roll") && !map.WasPressed("Crouch"), "First tap of a double tap fired");
        Pad(false); Pad(true, pressed: true); Require(map.WasPressed("Roll"), "Double tap did not fire");
        var crouched = false;
        for (var frame = 0; frame < 10; frame++) { Pad(false); crouched |= map.WasPressed("Crouch"); }
        Require(!crouched, "Exclusive single tap fired after a double tap");

        Pad(true, pressed: true); Pad(false);
        int crouches = 0, rolls = 0;
        for (var frame = 0; frame < 10; frame++) { Pad(false); crouches += map.WasPressed("Crouch") ? 1 : 0; rolls += map.WasPressed("Roll") ? 1 : 0; }
        Require(crouches == 1 && rolls == 0, "Single tap did not crouch exactly once after the window");

        Pad(true, pressed: true); Pad(false, dt: .3f); Pad(true, pressed: true, dt: .05f);
        Require(!map.WasPressed("Roll"), "Presses further apart than the window counted as a double tap");
        Pad(false); map.Reset(); Pad(false, dt: .3f);
        Require(!map.WasPressed("Crouch"), "Reset kept a pending tap");

        map.Actions[0].Bindings[0].Trigger = new() { Kind = InputTriggerKind.Hold, Duration = 0 };
        Key(true, pressed: true); Require(map.WasPressed("Roll"), "Replaced trigger was not picked up");
        Key(false, released: true);

        Require(InputPrompts.Binding(map, "Walk", InputDevice.KeyboardMouse) == "HOLD W", "Hold prompt not spelled out");
        Require(InputPrompts.Binding(map, "Roll", InputDevice.Controller) == "B x2", "Multi-tap prompt not spelled out");
        Require(InputPrompts.Binding(map, "Crouch", InputDevice.Controller) == "B", "Single tap prompt decorated");

        var loaded = InputActionMap.FromJson("""
            { "Actions": [ { "Name": "Roll", "Bindings": [
                { "Kind": "GamepadButton", "Button": "B", "Trigger": { "Kind": "MultiTap", "Count": 3, "Window": 0.4, "Exclusive": true } },
                { "Kind": "Key", "Key": "LeftAlt" } ] } ] }
            """);
        var parsed = loaded.Actions[0].Bindings[0].Trigger;
        Require(parsed is { Kind: InputTriggerKind.MultiTap, Count: 3, Exclusive: true } && Math.Abs(parsed.Window - .4f) < 1e-5f &&
                loaded.Actions[0].Bindings[1].Trigger is null, "Trigger JSON not read, or a missing trigger was invented");
        Console.WriteLine("PASS: tap/hold sharing a key, single-frame taps, double vs exclusive single tap, tap windows, reset, live trigger edits, and trigger prompts");
    }
}
