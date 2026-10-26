using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    // Mirrors PlutoGE::scripting::NativeComponentRequest (ComponentScriptApi.h); both sides check the size.
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeComponentRequest
    {
        public int Operation;
        public uint Entity;
        public nint TypeName;
        public nint Property;
        public nint Value;
        public int Result;
        public uint ResultEntity;
        public nint ResultText;
        public NativeUIEventRecord* Events;
        public int EventCapacity;
        public ulong Sequence;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeUIEventRecord
    {
        public int Type;
        public uint Entity;
    }

    internal enum ComponentOperation
    {
        HasComponent = 0,
        AddComponent = 1,
        RemoveComponent = 2,
        GetProperty = 3,
        SetProperty = 4,
        GetUIFocus = 5,
        SetUIFocus = 6,
        IsPointerOverUI = 7,
        IsKeyboardCaptured = 8,
        PollUIEvents = 9,
    }

    private static delegate* unmanaged[Cdecl]<NativeComponentRequest*, void> _controlComponents;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterComponentPropertyApi")]
    public static int RegisterComponentPropertyApi(delegate* unmanaged[Cdecl]<NativeComponentRequest*, void> control)
    {
        if (control == null || sizeof(NativeComponentRequest) != 72 || sizeof(NativeUIEventRecord) != 8) return 0;
        _controlComponents = control;
        return 1;
    }

    // Runs a request naming a component type (and optionally a property and value).
    private static bool ControlComponent(uint entity, ComponentOperation operation, string typeName,
                                         string? property, string? value, out NativeComponentRequest request)
    {
        request = default;
        if (_controlComponents == null) return false;
        var typeBytes = Utf8(typeName);
        var propertyBytes = Utf8(property);
        var valueBytes = Utf8(value);
        fixed (byte* type = typeBytes)
        fixed (byte* name = propertyBytes)
        fixed (byte* text = valueBytes)
        {
            request.Operation = (int)operation;
            request.Entity = entity;
            request.TypeName = (nint)type;
            request.Property = property is null ? 0 : (nint)name;
            request.Value = value is null ? 0 : (nint)text;
            fixed (NativeComponentRequest* pointer = &request)
                _controlComponents(pointer);
        }
        return request.Result != 0;
    }

    private static NativeComponentRequest ControlUI(ComponentOperation operation, uint entity = 0)
    {
        var request = new NativeComponentRequest { Operation = (int)operation, Entity = entity };
        if (_controlComponents != null)
            _controlComponents(&request);
        return request;
    }

    internal static bool HasRegisteredComponent(uint entity, string typeName) =>
        ControlComponent(entity, ComponentOperation.HasComponent, typeName, null, null, out _);

    internal static bool AddRegisteredComponent(uint entity, string typeName) =>
        ControlComponent(entity, ComponentOperation.AddComponent, typeName, null, null, out _);

    internal static bool RemoveRegisteredComponent(uint entity, string typeName) =>
        ControlComponent(entity, ComponentOperation.RemoveComponent, typeName, null, null, out _);

    // The property's serialized text, or null when the component or property does not exist.
    internal static string? GetComponentProperty(uint entity, string typeName, string property)
    {
        if (!ControlComponent(entity, ComponentOperation.GetProperty, typeName, property, null, out var request))
            return null;
        return Marshal.PtrToStringUTF8(request.ResultText) ?? string.Empty;
    }

    internal static bool SetComponentProperty(uint entity, string typeName, string property, string value) =>
        ControlComponent(entity, ComponentOperation.SetProperty, typeName, property, value, out _);

    internal static uint GetUIFocus() => ControlUI(ComponentOperation.GetUIFocus).ResultEntity;
    internal static void SetUIFocus(uint entity) => ControlUI(ComponentOperation.SetUIFocus, entity);
    internal static bool IsPointerOverUI() => ControlUI(ComponentOperation.IsPointerOverUI).Result != 0;
    internal static bool IsUIKeyboardCaptured() => ControlUI(ComponentOperation.IsKeyboardCaptured).Result != 0;

    // Copies the latest native UI frame's events into `events`; returns how many and that frame's number.
    internal static int PollUIEvents(Span<NativeUIEventRecord> events, out ulong sequence)
    {
        sequence = 0;
        if (_controlComponents == null) return 0;
        fixed (NativeUIEventRecord* buffer = events)
        {
            var request = new NativeComponentRequest
            {
                Operation = (int)ComponentOperation.PollUIEvents,
                Events = buffer,
                EventCapacity = events.Length,
            };
            _controlComponents(&request);
            sequence = request.Sequence;
            return request.Result;
        }
    }
}
