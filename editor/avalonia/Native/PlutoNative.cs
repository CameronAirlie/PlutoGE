using System.Runtime.InteropServices;

namespace PlutoGE.Editor.Avalonia.Native;

internal static unsafe partial class PlutoNative
{
    internal const uint ApiVersion = 10;
    internal const string Library = "PlutoGE.Editor.Native";

    internal enum Result : int
    {
        Ok = 0,
        InvalidArgument = 1,
        InvalidHandle = 2,
        AlreadyInitialized = 3,
        OpenGlUnavailable = 4,
        ContextNotShared = 5,
        InternalError = 6,
    }

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    internal delegate nint GlProcAddress(nint name, nint userData);

    [StructLayout(LayoutKind.Sequential)]
    internal struct EngineConfig
    {
        internal uint StructSize;
        internal uint ApiVersion;
        internal int InitialWidth;
        internal int InitialHeight;
        internal nint GetProcAddress;
        internal nint UserData;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ViewportFrame
    {
        internal uint StructSize;
        internal int Width;
        internal int Height;
        internal int Framebuffer;
        internal float DeltaSeconds;
        internal float TargetRefreshHz;
        internal float MouseX;
        internal float MouseY;
        internal float MouseWheel;
        internal byte MouseLeft;
        internal byte MouseRight;
        internal byte MouseMiddle;
        internal byte Focused;
        internal float CameraX;
        internal float CameraY;
        internal float CameraZ;
        internal float CameraYawDegrees;
        internal float CameraPitchDegrees;
        internal float CameraFovDegrees;
        internal float CameraNearPlane;
        internal float CameraFarPlane;
        internal nint GetProcAddress;
        internal nint UserData;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct EntityInfo
    {
        internal uint Id;
        internal uint ParentId;
        internal byte Active;
        internal fixed byte Name[120];

        internal string GetName()
        {
            fixed (byte* name = Name)
                return Marshal.PtrToStringUTF8((nint)name) ?? string.Empty;
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct Transform
    {
        internal fixed float Position[3];
        internal fixed float Rotation[3];
        internal fixed float Scale[3];
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct FrameStats
    {
        internal ulong FrameCount;
        internal double AverageFrameMs;
        internal double MaximumFrameMs;
        internal double ResizeMs;
        internal int Width;
        internal int Height;
        internal float TargetRefreshHz;
        internal float GpuFrameMs;
    }

    internal enum ProjectAssetType
    {
        Unknown,
        Scene,
        Prefab,
        Script,
        Mesh,
        Animation,
        AnimationClip,
        Model,
        Material,
        ShaderGraph,
        AnimationGraph,
        ParticleSystem,
        PostProcessPreset,
        Audio,
        Texture,
        Assembly,
        ScriptableObject,
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ProjectInfo
    {
        internal fixed byte Name[120];
        internal fixed byte ManifestPath[512];
        internal fixed byte AssetDirectory[256];
        internal fixed byte StartupScene[256];

        internal string GetName() { fixed (byte* value = Name) return ReadUtf8(value); }
        internal string GetManifestPath() { fixed (byte* value = ManifestPath) return ReadUtf8(value); }
        internal string GetAssetDirectory() { fixed (byte* value = AssetDirectory) return ReadUtf8(value); }
        internal string GetStartupScene() { fixed (byte* value = StartupScene) return ReadUtf8(value); }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AssetInfo
    {
        internal ProjectAssetType Type;
        internal ulong Size;
        internal fixed byte Reference[512];

        internal string GetReference() { fixed (byte* value = Reference) return ReadUtf8(value); }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct SceneInfo
    {
        internal fixed byte Path[512];
        internal string GetPath() { fixed (byte* value = Path) return ReadUtf8(value); }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ProjectSettings
    {
        internal fixed byte Name[120];
        internal fixed byte WindowTitle[120];
        internal fixed byte StartupScene[256];
        internal fixed byte ScriptAssembly[512];
        internal int WindowWidth;
        internal int WindowHeight;
        internal byte VSyncEnabled;
        internal float EditorFontSize;

        internal string GetName() { fixed (byte* value = Name) return ReadUtf8(value); }
        internal string GetWindowTitle() { fixed (byte* value = WindowTitle) return ReadUtf8(value); }
        internal string GetStartupScene() { fixed (byte* value = StartupScene) return ReadUtf8(value); }
        internal string GetScriptAssembly() { fixed (byte* value = ScriptAssembly) return ReadUtf8(value); }
        internal void SetName(string value) { fixed (byte* target = Name) WriteUtf8(target, 120, value); }
        internal void SetWindowTitle(string value) { fixed (byte* target = WindowTitle) WriteUtf8(target, 120, value); }
        internal void SetStartupScene(string value) { fixed (byte* target = StartupScene) WriteUtf8(target, 256, value); }
        internal void SetScriptAssembly(string value) { fixed (byte* target = ScriptAssembly) WriteUtf8(target, 512, value); }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ComponentInfo
    {
        internal uint Index;
        internal byte Enabled;
        internal fixed byte Name[120];
        internal string GetName() { fixed (byte* value = Name) return ReadUtf8(value); }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct ComponentProperty
    {
        internal int Type;
        internal byte Editable;
        internal fixed byte Name[120];
        internal fixed byte Value[512];
        internal fixed byte EnumOptions[512];
        internal string GetName() { fixed (byte* value = Name) return ReadUtf8(value); }
        internal string GetValue() { fixed (byte* value = Value) return ReadUtf8(value); }
        internal string[] GetEnumOptions()
        {
            fixed (byte* value = EnumOptions)
                return ReadUtf8(value).Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct AddableComponentType
    {
        internal byte CanAdd;
        internal fixed byte TypeName[120];
        internal fixed byte DisplayName[120];
        internal fixed byte Category[64];
        internal string GetTypeName() { fixed (byte* value = TypeName) return ReadUtf8(value); }
        internal string GetDisplayName() { fixed (byte* value = DisplayName) return ReadUtf8(value); }
        internal string GetCategory() { fixed (byte* value = Category) return ReadUtf8(value); }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct PostProcessEffectInfo
    {
        internal uint Index;
        internal byte Enabled;
        internal fixed byte TypeName[120];
        internal fixed byte DisplayName[120];
        internal string GetTypeName() { fixed (byte* value = TypeName) return ReadUtf8(value); }
        internal string GetDisplayName() { fixed (byte* value = DisplayName) return ReadUtf8(value); }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct PostProcessParameter
    {
        internal int Type;
        internal fixed byte Name[120];
        internal fixed byte Value[512];
        internal fixed byte EnumOptions[512];
        internal string GetName() { fixed (byte* value = Name) return ReadUtf8(value); }
        internal string GetValue() { fixed (byte* value = Value) return ReadUtf8(value); }
        internal string[] GetEnumOptions()
        {
            fixed (byte* value = EnumOptions)
                return ReadUtf8(value).Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        }
    }

    [LibraryImport(Library, EntryPoint = "pluto_editor_api_version")]
    internal static partial uint ApiVersionNative();

    [LibraryImport(Library, EntryPoint = "pluto_editor_engine_create")]
    internal static partial Result EngineCreate(in EngineConfig config, out ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_engine_destroy")]
    internal static partial Result EngineDestroy(ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_engine_acquire_render_context")]
    internal static partial Result EngineAcquireRenderContext(ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_engine_release_render_context")]
    internal static partial Result EngineReleaseRenderContext(ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_viewport_create")]
    internal static partial Result ViewportCreate(ulong engine, out ulong viewport);

    [LibraryImport(Library, EntryPoint = "pluto_editor_viewport_destroy")]
    internal static partial Result ViewportDestroy(ulong engine, ulong viewport);

    [LibraryImport(Library, EntryPoint = "pluto_editor_viewport_render")]
    internal static partial Result ViewportRender(ulong engine, ulong viewport, in ViewportFrame frame);

    [LibraryImport(Library, EntryPoint = "pluto_editor_viewport_set_selected_entity")]
    internal static partial Result ViewportSetSelectedEntity(ulong engine, ulong viewport, uint entityId);

    [LibraryImport(Library, EntryPoint = "pluto_editor_viewport_pick_entity")]
    internal static partial Result ViewportPickEntity(ulong engine, ulong viewport, float mouseX, float mouseY, out uint entityId);

    [LibraryImport(Library, EntryPoint = "pluto_editor_viewport_set_gizmo_operation")]
    internal static partial Result ViewportSetGizmoOperation(ulong engine, ulong viewport, int operation);

    [LibraryImport(Library, EntryPoint = "pluto_editor_viewport_get_gizmo_active")]
    internal static partial Result ViewportGetGizmoActive(ulong engine, ulong viewport, out byte active);

    [LibraryImport(Library, EntryPoint = "pluto_editor_viewport_get_stats")]
    internal static partial Result ViewportGetStats(ulong engine, ulong viewport, out FrameStats stats);

    [LibraryImport(Library, EntryPoint = "pluto_editor_scene_get_entity_count")]
    internal static partial Result SceneGetEntityCount(ulong engine, out uint count);

    [LibraryImport(Library, EntryPoint = "pluto_editor_scene_get_entity")]
    internal static partial Result SceneGetEntity(ulong engine, uint index, out EntityInfo entity);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_get_active")]
    internal static partial Result EntityGetActive(ulong engine, uint entityId, out byte active);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_set_active")]
    internal static partial Result EntitySetActive(ulong engine, uint entityId, byte active);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_get_transform")]
    internal static partial Result EntityGetTransform(ulong engine, uint entityId, out Transform transform);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_set_transform")]
    internal static partial Result EntitySetTransform(ulong engine, uint entityId, in Transform transform);

    [LibraryImport(Library, EntryPoint = "pluto_editor_project_load", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result ProjectLoad(ulong engine, string manifestPath);

    [LibraryImport(Library, EntryPoint = "pluto_editor_project_get_info")]
    internal static partial Result ProjectGetInfo(ulong engine, out ProjectInfo project);

    [LibraryImport(Library, EntryPoint = "pluto_editor_project_get_asset_count")]
    internal static partial Result ProjectGetAssetCount(ulong engine, out uint count);

    [LibraryImport(Library, EntryPoint = "pluto_editor_project_get_asset")]
    internal static partial Result ProjectGetAsset(ulong engine, uint index, out AssetInfo asset);

    [LibraryImport(Library, EntryPoint = "pluto_editor_project_refresh")]
    internal static partial Result ProjectRefresh(ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_project_save")]
    internal static partial Result ProjectSave(ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_project_get_settings")]
    internal static partial Result ProjectGetSettings(ulong engine, out ProjectSettings settings);

    [LibraryImport(Library, EntryPoint = "pluto_editor_project_set_settings")]
    internal static partial Result ProjectSetSettings(ulong engine, in ProjectSettings settings);

    [LibraryImport(Library, EntryPoint = "pluto_editor_scene_new")]
    internal static partial Result SceneNew(ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_scene_load", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result SceneLoad(ulong engine, string pathOrReference);

    [LibraryImport(Library, EntryPoint = "pluto_editor_scene_save", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result SceneSave(ulong engine, string? path);

    [LibraryImport(Library, EntryPoint = "pluto_editor_scene_get_info")]
    internal static partial Result SceneGetInfo(ulong engine, out SceneInfo scene);

    [LibraryImport(Library, EntryPoint = "pluto_editor_runtime_start")]
    internal static partial Result RuntimeStart(ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_runtime_stop")]
    internal static partial Result RuntimeStop(ulong engine);

    [LibraryImport(Library, EntryPoint = "pluto_editor_runtime_is_running")]
    internal static partial Result RuntimeIsRunning(ulong engine, out byte running);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_create", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result EntityCreate(ulong engine, uint parentId, string name, out uint entityId);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_duplicate")]
    internal static partial Result EntityDuplicate(ulong engine, uint sourceId, out uint entityId);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_delete")]
    internal static partial Result EntityDelete(ulong engine, uint entityId);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_set_name", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result EntitySetName(ulong engine, uint entityId, string name);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_get_component_count")]
    internal static partial Result EntityGetComponentCount(ulong engine, uint entityId, out uint count);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_get_component")]
    internal static partial Result EntityGetComponent(ulong engine, uint entityId, uint componentIndex, out ComponentInfo component);

    [LibraryImport(Library, EntryPoint = "pluto_editor_component_set_enabled")]
    internal static partial Result ComponentSetEnabled(ulong engine, uint entityId, uint componentIndex, byte enabled);

    [LibraryImport(Library, EntryPoint = "pluto_editor_component_get_property_count")]
    internal static partial Result ComponentGetPropertyCount(ulong engine, uint entityId, uint componentIndex, out uint count);

    [LibraryImport(Library, EntryPoint = "pluto_editor_component_get_property")]
    internal static partial Result ComponentGetProperty(ulong engine, uint entityId, uint componentIndex, uint propertyIndex, out ComponentProperty property);

    [LibraryImport(Library, EntryPoint = "pluto_editor_component_set_property", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result ComponentSetProperty(ulong engine, uint entityId, uint componentIndex, uint propertyIndex, string value);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_get_addable_component_type_count")]
    internal static partial Result EntityGetAddableComponentTypeCount(ulong engine, uint entityId, out uint count);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_get_addable_component_type")]
    internal static partial Result EntityGetAddableComponentType(ulong engine, uint entityId, uint typeIndex, out AddableComponentType componentType);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_add_component", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result EntityAddComponent(ulong engine, uint entityId, string typeName);

    [LibraryImport(Library, EntryPoint = "pluto_editor_entity_remove_component")]
    internal static partial Result EntityRemoveComponent(ulong engine, uint entityId, uint componentIndex);

    [LibraryImport(Library, EntryPoint = "pluto_editor_post_process_get_registered_type_count")]
    internal static partial Result PostProcessGetRegisteredTypeCount(ulong engine, out uint count);

    [LibraryImport(Library, EntryPoint = "pluto_editor_post_process_get_registered_type")]
    internal static partial Result PostProcessGetRegisteredType(ulong engine, uint typeIndex, byte* typeName, uint typeNameSize);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_get_post_process_effect_count")]
    internal static partial Result EditorCameraGetPostProcessEffectCount(ulong engine, out uint count);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_get_post_process_effect")]
    internal static partial Result EditorCameraGetPostProcessEffect(ulong engine, uint effectIndex, out PostProcessEffectInfo effect);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_add_post_process_effect", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result EditorCameraAddPostProcessEffect(ulong engine, string typeName);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_remove_post_process_effect")]
    internal static partial Result EditorCameraRemovePostProcessEffect(ulong engine, uint effectIndex);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_move_post_process_effect")]
    internal static partial Result EditorCameraMovePostProcessEffect(ulong engine, uint fromIndex, uint toIndex);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_set_post_process_effect_enabled")]
    internal static partial Result EditorCameraSetPostProcessEffectEnabled(ulong engine, uint effectIndex, byte enabled);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_get_post_process_parameter_count")]
    internal static partial Result EditorCameraGetPostProcessParameterCount(ulong engine, uint effectIndex, out uint count);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_get_post_process_parameter")]
    internal static partial Result EditorCameraGetPostProcessParameter(ulong engine, uint effectIndex, uint parameterIndex, out PostProcessParameter parameter);

    [LibraryImport(Library, EntryPoint = "pluto_editor_camera_set_post_process_parameter", StringMarshalling = StringMarshalling.Utf8)]
    internal static partial Result EditorCameraSetPostProcessParameter(ulong engine, uint effectIndex, uint parameterIndex, string value);

    [LibraryImport(Library, EntryPoint = "pluto_editor_get_last_error")]
    private static partial nint GetLastErrorNative();

    internal static string GetLastError() => Marshal.PtrToStringUTF8(GetLastErrorNative()) ?? "Unknown native error";

    private static string ReadUtf8(byte* value) => Marshal.PtrToStringUTF8((nint)value) ?? string.Empty;

    private static void WriteUtf8(byte* target, int capacity, string? value)
    {
        new Span<byte>(target, capacity).Clear();
        var source = System.Text.Encoding.UTF8.GetBytes(value ?? string.Empty);
        source.AsSpan(0, Math.Min(source.Length, capacity - 1)).CopyTo(new Span<byte>(target, capacity));
    }

    internal static void ThrowIfFailed(Result result, string operation)
    {
        if (result != Result.Ok)
            throw new InvalidOperationException($"{operation} failed ({result}): {GetLastError()}");
    }
}
