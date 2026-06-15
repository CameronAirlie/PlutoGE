using System.Collections.Concurrent;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe class ScriptBridge
{
    [StructLayout(LayoutKind.Sequential)]
    internal struct NativeVector3
    {
        public float X;
        public float Y;
        public float Z;

        public NativeVector3(float x, float y, float z)
        {
            X = x;
            Y = y;
            Z = z;
        }

        public readonly Vector3 ToManaged()
        {
            return new Vector3(X, Y, Z);
        }

        public static NativeVector3 FromManaged(Vector3 value)
        {
            return new NativeVector3(value.X, value.Y, value.Z);
        }
    }

    internal enum NativeComponentType
    {
        Mesh = 0,
        Camera = 1,
        Light = 2,
        Script = 3,
        Rigidbody = 4,
        Collider = 5,
    }

    private sealed class ScriptLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver _resolver;

        public ScriptLoadContext(string mainAssemblyPath)
            : base($"PlutoGE-Scripts:{Path.GetFileNameWithoutExtension(mainAssemblyPath)}", isCollectible: true)
        {
            _resolver = new AssemblyDependencyResolver(mainAssemblyPath);
        }

        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (assemblyName.Name == typeof(ScriptBehaviour).Assembly.GetName().Name)
            {
                return typeof(ScriptBehaviour).Assembly;
            }

            var resolvedPath = _resolver.ResolveAssemblyToPath(assemblyName);
            return resolvedPath is null ? null : LoadFromAssemblyPath(resolvedPath);
        }
    }

    private sealed record ScriptFieldMetadata(string Name, int Type, object? DefaultValue, MemberInfo Member);

    private sealed record ScriptClassMetadata(string AssemblyName, string NamespaceName, string ClassName, Type Type, IReadOnlyList<ScriptFieldMetadata> Fields)
    {
        public string FullName => string.IsNullOrEmpty(NamespaceName) ? ClassName : $"{NamespaceName}.{ClassName}";
    }

    private static readonly ConcurrentDictionary<long, ScriptBehaviour> Instances = new();
    private static readonly Dictionary<string, ScriptClassMetadata> ScriptClasses = new(StringComparer.Ordinal);
    private static readonly object Gate = new();

    private static ScriptLoadContext? _loadContext;
    private static Assembly? _loadedAssembly;
    private static long _nextInstanceHandle;
    private static string _lastError = string.Empty;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityPosition;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityScale;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityScale;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityForward;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityRight;
    private static delegate* unmanaged[Cdecl]<uint, int> _getEntityActive;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setEntityActive;
    private static delegate* unmanaged[Cdecl]<uint, int, int> _hasComponent;
    private static delegate* unmanaged[Cdecl]<uint, int, int> _getComponentEnabled;
    private static delegate* unmanaged[Cdecl]<uint, int, int, void> _setComponentEnabled;
    private static delegate* unmanaged[Cdecl]<uint, int> _getCameraMain;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setCameraMain;
    private static delegate* unmanaged[Cdecl]<uint, float> _getCameraFov;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setCameraFov;
    private static delegate* unmanaged[Cdecl]<uint, float> _getLightIntensity;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setLightIntensity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getLightColor;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setLightColor;
    private static delegate* unmanaged[Cdecl]<uint, int> _getMeshStatic;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setMeshStatic;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRigidbodyMass;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRigidbodyMass;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRigidbodyLinearDrag;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRigidbodyLinearDrag;
    private static delegate* unmanaged[Cdecl]<uint, float> _getRigidbodyAngularDrag;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setRigidbodyAngularDrag;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRigidbodyUseGravity;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRigidbodyUseGravity;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRigidbodyKinematic;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRigidbodyKinematic;
    private static delegate* unmanaged[Cdecl]<uint, int> _getRigidbodyFreezeRotation;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setRigidbodyFreezeRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getRigidbodyVelocity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setRigidbodyVelocity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getRigidbodyAngularVelocity;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setRigidbodyAngularVelocity;
    private static delegate* unmanaged[Cdecl]<uint, int> _getColliderShape;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setColliderShape;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getColliderCenter;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setColliderCenter;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getColliderSize;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setColliderSize;
    private static delegate* unmanaged[Cdecl]<uint, float> _getColliderRadius;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setColliderRadius;
    private static delegate* unmanaged[Cdecl]<uint, float> _getColliderHeight;
    private static delegate* unmanaged[Cdecl]<uint, float, void> _setColliderHeight;
    private static delegate* unmanaged[Cdecl]<uint, int> _getColliderTrigger;
    private static delegate* unmanaged[Cdecl]<uint, int, void> _setColliderTrigger;
    private static delegate* unmanaged[Cdecl]<int, int> _getKeyDown;
    private static delegate* unmanaged[Cdecl]<int, int> _getKeyPressed;
    private static delegate* unmanaged[Cdecl]<int, int> _getKeyReleased;
    private static delegate* unmanaged[Cdecl]<int, int> _getMouseButtonDown;
    private static delegate* unmanaged[Cdecl]<int, int> _getMouseButtonPressed;
    private static delegate* unmanaged[Cdecl]<int, int> _getMouseButtonReleased;
    private static delegate* unmanaged[Cdecl]<NativeVector3> _getMousePosition;
    private static delegate* unmanaged[Cdecl]<NativeVector3> _getMouseDelta;
    private static delegate* unmanaged[Cdecl]<NativeVector3> _getMouseScrollDelta;
    private static delegate* unmanaged[Cdecl]<int> _getQuitRequested;
    private static delegate* unmanaged[Cdecl]<int> _getCursorLocked;
    private static delegate* unmanaged[Cdecl]<int, void> _setCursorLocked;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "LoadScriptAssembly")]
    public static int LoadScriptAssembly(nint assemblyPathPtr)
    {
        try
        {
            var assemblyPath = Marshal.PtrToStringUTF8(assemblyPathPtr);
            if (string.IsNullOrWhiteSpace(assemblyPath))
            {
                SetError("Managed assembly path was empty.");
                return 0;
            }

            lock (Gate)
            {
                ResetLoadedAssembly();

                var fullPath = Path.GetFullPath(assemblyPath);
                _loadContext = new ScriptLoadContext(fullPath);
                _loadedAssembly = _loadContext.LoadFromAssemblyPath(fullPath);

                ScriptClasses.Clear();
                foreach (var scriptClass in DiscoverScriptClasses(_loadedAssembly))
                {
                    ScriptClasses[scriptClass.FullName] = scriptClass;
                }

                _lastError = string.Empty;
                return 1;
            }
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "GetScriptMetadata")]
    public static nint GetScriptMetadata()
    {
        lock (Gate)
        {
            var metadata = BuildMetadataPayload();
            return Marshal.StringToCoTaskMemUTF8(metadata);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "GetLastError")]
    public static nint GetLastError()
    {
        lock (Gate)
        {
            return Marshal.StringToCoTaskMemUTF8(_lastError);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "FreeNativeString")]
    public static void FreeNativeString(nint textPtr)
    {
        if (textPtr != 0)
        {
            Marshal.FreeCoTaskMem(textPtr);
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterGameObjectApi")]
    public static int RegisterGameObjectApi(
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityPosition,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityRotation,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityRotation,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityScale,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityScale,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityForward,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityRight,
        delegate* unmanaged[Cdecl]<uint, int> getEntityActive,
        delegate* unmanaged[Cdecl]<uint, int, void> setEntityActive)
    {
        if (getEntityPosition == null ||
            setEntityPosition == null ||
            getEntityRotation == null ||
            setEntityRotation == null ||
            getEntityScale == null ||
            setEntityScale == null ||
            getEntityForward == null ||
            getEntityRight == null ||
            getEntityActive == null ||
            setEntityActive == null)
        {
            SetError("Managed game object API registration received a null function pointer.");
            return 0;
        }

        _getEntityPosition = getEntityPosition;
        _setEntityPosition = setEntityPosition;
        _getEntityRotation = getEntityRotation;
        _setEntityRotation = setEntityRotation;
        _getEntityScale = getEntityScale;
        _setEntityScale = setEntityScale;
        _getEntityForward = getEntityForward;
        _getEntityRight = getEntityRight;
        _getEntityActive = getEntityActive;
        _setEntityActive = setEntityActive;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterComponentApi")]
    public static int RegisterComponentApi(
        delegate* unmanaged[Cdecl]<uint, int, int> hasComponent,
        delegate* unmanaged[Cdecl]<uint, int, int> getComponentEnabled,
        delegate* unmanaged[Cdecl]<uint, int, int, void> setComponentEnabled)
    {
        if (hasComponent == null || getComponentEnabled == null || setComponentEnabled == null)
        {
            SetError("Managed component API registration received a null function pointer.");
            return 0;
        }

        _hasComponent = hasComponent;
        _getComponentEnabled = getComponentEnabled;
        _setComponentEnabled = setComponentEnabled;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterCameraComponentApi")]
    public static int RegisterCameraComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getCameraMain,
        delegate* unmanaged[Cdecl]<uint, int, void> setCameraMain,
        delegate* unmanaged[Cdecl]<uint, float> getCameraFov,
        delegate* unmanaged[Cdecl]<uint, float, void> setCameraFov)
    {
        if (getCameraMain == null || setCameraMain == null || getCameraFov == null || setCameraFov == null)
        {
            SetError("Managed camera component API registration received a null function pointer.");
            return 0;
        }

        _getCameraMain = getCameraMain;
        _setCameraMain = setCameraMain;
        _getCameraFov = getCameraFov;
        _setCameraFov = setCameraFov;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterLightComponentApi")]
    public static int RegisterLightComponentApi(
        delegate* unmanaged[Cdecl]<uint, float> getLightIntensity,
        delegate* unmanaged[Cdecl]<uint, float, void> setLightIntensity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getLightColor,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setLightColor)
    {
        if (getLightIntensity == null || setLightIntensity == null || getLightColor == null || setLightColor == null)
        {
            SetError("Managed light component API registration received a null function pointer.");
            return 0;
        }

        _getLightIntensity = getLightIntensity;
        _setLightIntensity = setLightIntensity;
        _getLightColor = getLightColor;
        _setLightColor = setLightColor;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterMeshComponentApi")]
    public static int RegisterMeshComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getMeshStatic,
        delegate* unmanaged[Cdecl]<uint, int, void> setMeshStatic)
    {
        if (getMeshStatic == null || setMeshStatic == null)
        {
            SetError("Managed mesh component API registration received a null function pointer.");
            return 0;
        }

        _getMeshStatic = getMeshStatic;
        _setMeshStatic = setMeshStatic;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterRigidbodyComponentApi")]
    public static int RegisterRigidbodyComponentApi(
        delegate* unmanaged[Cdecl]<uint, float> getMass,
        delegate* unmanaged[Cdecl]<uint, float, void> setMass,
        delegate* unmanaged[Cdecl]<uint, float> getLinearDrag,
        delegate* unmanaged[Cdecl]<uint, float, void> setLinearDrag,
        delegate* unmanaged[Cdecl]<uint, float> getAngularDrag,
        delegate* unmanaged[Cdecl]<uint, float, void> setAngularDrag,
        delegate* unmanaged[Cdecl]<uint, int> getUseGravity,
        delegate* unmanaged[Cdecl]<uint, int, void> setUseGravity,
        delegate* unmanaged[Cdecl]<uint, int> getKinematic,
        delegate* unmanaged[Cdecl]<uint, int, void> setKinematic,
        delegate* unmanaged[Cdecl]<uint, int> getFreezeRotation,
        delegate* unmanaged[Cdecl]<uint, int, void> setFreezeRotation,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getVelocity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setVelocity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getAngularVelocity,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setAngularVelocity)
    {
        if (getMass == null || setMass == null || getLinearDrag == null || setLinearDrag == null ||
            getAngularDrag == null || setAngularDrag == null || getUseGravity == null || setUseGravity == null ||
            getKinematic == null || setKinematic == null || getFreezeRotation == null || setFreezeRotation == null ||
            getVelocity == null || setVelocity == null || getAngularVelocity == null || setAngularVelocity == null)
        {
            SetError("Managed rigidbody component API registration received a null function pointer.");
            return 0;
        }

        _getRigidbodyMass = getMass;
        _setRigidbodyMass = setMass;
        _getRigidbodyLinearDrag = getLinearDrag;
        _setRigidbodyLinearDrag = setLinearDrag;
        _getRigidbodyAngularDrag = getAngularDrag;
        _setRigidbodyAngularDrag = setAngularDrag;
        _getRigidbodyUseGravity = getUseGravity;
        _setRigidbodyUseGravity = setUseGravity;
        _getRigidbodyKinematic = getKinematic;
        _setRigidbodyKinematic = setKinematic;
        _getRigidbodyFreezeRotation = getFreezeRotation;
        _setRigidbodyFreezeRotation = setFreezeRotation;
        _getRigidbodyVelocity = getVelocity;
        _setRigidbodyVelocity = setVelocity;
        _getRigidbodyAngularVelocity = getAngularVelocity;
        _setRigidbodyAngularVelocity = setAngularVelocity;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterColliderComponentApi")]
    public static int RegisterColliderComponentApi(
        delegate* unmanaged[Cdecl]<uint, int> getShape,
        delegate* unmanaged[Cdecl]<uint, int, void> setShape,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getCenter,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setCenter,
        delegate* unmanaged[Cdecl]<uint, NativeVector3> getSize,
        delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setSize,
        delegate* unmanaged[Cdecl]<uint, float> getRadius,
        delegate* unmanaged[Cdecl]<uint, float, void> setRadius,
        delegate* unmanaged[Cdecl]<uint, float> getHeight,
        delegate* unmanaged[Cdecl]<uint, float, void> setHeight,
        delegate* unmanaged[Cdecl]<uint, int> getTrigger,
        delegate* unmanaged[Cdecl]<uint, int, void> setTrigger)
    {
        if (getShape == null || setShape == null || getCenter == null || setCenter == null ||
            getSize == null || setSize == null || getRadius == null || setRadius == null ||
            getHeight == null || setHeight == null || getTrigger == null || setTrigger == null)
        {
            SetError("Managed collider component API registration received a null function pointer.");
            return 0;
        }

        _getColliderShape = getShape;
        _setColliderShape = setShape;
        _getColliderCenter = getCenter;
        _setColliderCenter = setCenter;
        _getColliderSize = getSize;
        _setColliderSize = setSize;
        _getColliderRadius = getRadius;
        _setColliderRadius = setRadius;
        _getColliderHeight = getHeight;
        _setColliderHeight = setHeight;
        _getColliderTrigger = getTrigger;
        _setColliderTrigger = setTrigger;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterInputApi")]
    public static int RegisterInputApi(
        delegate* unmanaged[Cdecl]<int, int> getKeyDown,
        delegate* unmanaged[Cdecl]<int, int> getKeyPressed,
        delegate* unmanaged[Cdecl]<int, int> getKeyReleased,
        delegate* unmanaged[Cdecl]<int, int> getMouseButtonDown,
        delegate* unmanaged[Cdecl]<int, int> getMouseButtonPressed,
        delegate* unmanaged[Cdecl]<int, int> getMouseButtonReleased,
        delegate* unmanaged[Cdecl]<NativeVector3> getMousePosition,
        delegate* unmanaged[Cdecl]<NativeVector3> getMouseDelta,
        delegate* unmanaged[Cdecl]<NativeVector3> getMouseScrollDelta,
        delegate* unmanaged[Cdecl]<int> getQuitRequested,
        delegate* unmanaged[Cdecl]<int> getCursorLocked,
        delegate* unmanaged[Cdecl]<int, void> setCursorLocked)
    {
        if (getKeyDown == null ||
            getKeyPressed == null ||
            getKeyReleased == null ||
            getMouseButtonDown == null ||
            getMouseButtonPressed == null ||
            getMouseButtonReleased == null ||
            getMousePosition == null ||
            getMouseDelta == null ||
            getMouseScrollDelta == null ||
            getQuitRequested == null ||
            getCursorLocked == null ||
            setCursorLocked == null)
        {
            SetError("Managed input API registration received a null function pointer.");
            return 0;
        }

        _getKeyDown = getKeyDown;
        _getKeyPressed = getKeyPressed;
        _getKeyReleased = getKeyReleased;
        _getMouseButtonDown = getMouseButtonDown;
        _getMouseButtonPressed = getMouseButtonPressed;
        _getMouseButtonReleased = getMouseButtonReleased;
        _getMousePosition = getMousePosition;
        _getMouseDelta = getMouseDelta;
        _getMouseScrollDelta = getMouseScrollDelta;
        _getQuitRequested = getQuitRequested;
        _getCursorLocked = getCursorLocked;
        _setCursorLocked = setCursorLocked;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "CreateScriptInstance")]
    public static long CreateScriptInstance(nint fullTypeNamePtr, uint entityId)
    {
        try
        {
            var fullTypeName = Marshal.PtrToStringUTF8(fullTypeNamePtr);
            if (string.IsNullOrWhiteSpace(fullTypeName))
            {
                SetError("Managed script type name was empty.");
                return 0;
            }

            lock (Gate)
            {
                if (!ScriptClasses.TryGetValue(fullTypeName, out var scriptClass))
                {
                    SetError($"Unknown managed script type '{fullTypeName}'.");
                    return 0;
                }

                if (Activator.CreateInstance(scriptClass.Type, nonPublic: true) is not ScriptBehaviour instance)
                {
                    SetError($"Failed to instantiate managed script type '{fullTypeName}'.");
                    return 0;
                }

                instance.EntityId = entityId;
                var handle = Interlocked.Increment(ref _nextInstanceHandle);
                Instances[handle] = instance;
                return handle;
            }
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "DestroyScriptInstance")]
    public static void DestroyScriptInstance(long handle)
    {
        Instances.TryRemove(handle, out _);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "UnloadScriptAssembly")]
    public static int UnloadScriptAssembly()
    {
        try
        {
            lock (Gate)
            {
                ResetLoadedAssembly();
                _lastError = string.Empty;
                return 1;
            }
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnCreate")]
    public static int InvokeOnCreate(long handle)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnCreate();
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnUpdate")]
    public static int InvokeOnUpdate(long handle, float deltaTime)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnUpdate(deltaTime);
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnCollisionEnter")]
    public static int InvokeOnCollisionEnter(long handle, uint otherEntityId)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnCollisionEnter(new GameObject(otherEntityId));
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "InvokeOnCollisionExit")]
    public static int InvokeOnCollisionExit(long handle, uint otherEntityId)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            instance.OnCollisionExit(new GameObject(otherEntityId));
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "ApplyFieldData")]
    public static int ApplyFieldData(long handle, nint fieldDataPtr)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            var fieldData = Marshal.PtrToStringUTF8(fieldDataPtr) ?? string.Empty;
            if (!ScriptClasses.TryGetValue(instance.GetType().FullName ?? string.Empty, out var scriptClass))
            {
                SetError($"Managed script metadata missing for '{instance.GetType().FullName}'.");
                return 0;
            }

            ApplyFieldValues(instance, scriptClass, fieldData);
            return 1;
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "GetFieldData")]
    public static nint GetFieldData(long handle)
    {
        try
        {
            if (!Instances.TryGetValue(handle, out var instance))
            {
                SetError($"Unknown managed script instance handle '{handle}'.");
                return 0;
            }

            if (!ScriptClasses.TryGetValue(instance.GetType().FullName ?? string.Empty, out var scriptClass))
            {
                SetError($"Managed script metadata missing for '{instance.GetType().FullName}'.");
                return 0;
            }

            _lastError = string.Empty;
            return Marshal.StringToCoTaskMemUTF8(BuildFieldData(instance, scriptClass));
        }
        catch (Exception exception)
        {
            SetError(exception.ToString());
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "SetEntityId")]
    public static int SetEntityId(long handle, uint entityId)
    {
        if (!Instances.TryGetValue(handle, out var instance))
        {
            SetError($"Unknown managed script instance handle '{handle}'.");
            return 0;
        }

        instance.EntityId = entityId;
        return 1;
    }

    internal static Vector3 GetEntityPosition(uint entityId)
    {
        return _getEntityPosition == null ? Vector3.Zero : _getEntityPosition(entityId).ToManaged();
    }

    internal static void SetEntityPosition(uint entityId, Vector3 position)
    {
        if (_setEntityPosition == null)
        {
            return;
        }

        _setEntityPosition(entityId, NativeVector3.FromManaged(position));
    }

    internal static Vector3 GetEntityRotation(uint entityId)
    {
        return _getEntityRotation == null ? Vector3.Zero : _getEntityRotation(entityId).ToManaged();
    }

    internal static void SetEntityRotation(uint entityId, Vector3 rotation)
    {
        if (_setEntityRotation == null)
        {
            return;
        }

        _setEntityRotation(entityId, NativeVector3.FromManaged(rotation));
    }

    internal static Vector3 GetEntityScale(uint entityId)
    {
        return _getEntityScale == null ? Vector3.One : _getEntityScale(entityId).ToManaged();
    }

    internal static Vector3 GetEntityForward(uint entityId)
    {
        return _getEntityForward == null ? -Vector3.UnitZ : _getEntityForward(entityId).ToManaged();
    }

    internal static Vector3 GetEntityRight(uint entityId)
    {
        return _getEntityRight == null ? Vector3.UnitX : _getEntityRight(entityId).ToManaged();
    }

    internal static void SetEntityScale(uint entityId, Vector3 scale)
    {
        if (_setEntityScale == null)
        {
            return;
        }

        _setEntityScale(entityId, NativeVector3.FromManaged(scale));
    }

    internal static bool GetEntityActive(uint entityId)
    {
        return _getEntityActive != null && _getEntityActive(entityId) != 0;
    }

    internal static void SetEntityActive(uint entityId, bool active)
    {
        if (_setEntityActive == null)
        {
            return;
        }

        _setEntityActive(entityId, active ? 1 : 0);
    }

    internal static bool HasComponent(uint entityId, NativeComponentType componentType)
    {
        return _hasComponent != null && _hasComponent(entityId, (int)componentType) != 0;
    }

    internal static bool GetComponentEnabled(uint entityId, NativeComponentType componentType)
    {
        return _getComponentEnabled != null && _getComponentEnabled(entityId, (int)componentType) != 0;
    }

    internal static void SetComponentEnabled(uint entityId, NativeComponentType componentType, bool enabled)
    {
        if (_setComponentEnabled == null)
        {
            return;
        }

        _setComponentEnabled(entityId, (int)componentType, enabled ? 1 : 0);
    }

    internal static bool GetCameraMain(uint entityId)
    {
        return _getCameraMain != null && _getCameraMain(entityId) != 0;
    }

    internal static void SetCameraMain(uint entityId, bool isMain)
    {
        if (_setCameraMain == null)
        {
            return;
        }

        _setCameraMain(entityId, isMain ? 1 : 0);
    }

    internal static float GetCameraFov(uint entityId)
    {
        return _getCameraFov == null ? 0.0f : _getCameraFov(entityId);
    }

    internal static void SetCameraFov(uint entityId, float fov)
    {
        if (_setCameraFov == null)
        {
            return;
        }

        _setCameraFov(entityId, fov);
    }

    internal static float GetLightIntensity(uint entityId)
    {
        return _getLightIntensity == null ? 0.0f : _getLightIntensity(entityId);
    }

    internal static void SetLightIntensity(uint entityId, float intensity)
    {
        if (_setLightIntensity == null)
        {
            return;
        }

        _setLightIntensity(entityId, intensity);
    }

    internal static Vector3 GetLightColor(uint entityId)
    {
        return _getLightColor == null ? Vector3.Zero : _getLightColor(entityId).ToManaged();
    }

    internal static void SetLightColor(uint entityId, Vector3 color)
    {
        if (_setLightColor == null)
        {
            return;
        }

        _setLightColor(entityId, NativeVector3.FromManaged(color));
    }

    internal static bool GetMeshStatic(uint entityId)
    {
        return _getMeshStatic != null && _getMeshStatic(entityId) != 0;
    }

    internal static void SetMeshStatic(uint entityId, bool isStatic)
    {
        if (_setMeshStatic == null)
        {
            return;
        }

        _setMeshStatic(entityId, isStatic ? 1 : 0);
    }

    internal static float GetRigidbodyMass(uint entityId) => _getRigidbodyMass == null ? 0.0f : _getRigidbodyMass(entityId);
    internal static void SetRigidbodyMass(uint entityId, float value) { if (_setRigidbodyMass != null) _setRigidbodyMass(entityId, value); }
    internal static float GetRigidbodyLinearDrag(uint entityId) => _getRigidbodyLinearDrag == null ? 0.0f : _getRigidbodyLinearDrag(entityId);
    internal static void SetRigidbodyLinearDrag(uint entityId, float value) { if (_setRigidbodyLinearDrag != null) _setRigidbodyLinearDrag(entityId, value); }
    internal static float GetRigidbodyAngularDrag(uint entityId) => _getRigidbodyAngularDrag == null ? 0.0f : _getRigidbodyAngularDrag(entityId);
    internal static void SetRigidbodyAngularDrag(uint entityId, float value) { if (_setRigidbodyAngularDrag != null) _setRigidbodyAngularDrag(entityId, value); }
    internal static bool GetRigidbodyUseGravity(uint entityId) => _getRigidbodyUseGravity != null && _getRigidbodyUseGravity(entityId) != 0;
    internal static void SetRigidbodyUseGravity(uint entityId, bool value) { if (_setRigidbodyUseGravity != null) _setRigidbodyUseGravity(entityId, value ? 1 : 0); }
    internal static bool GetRigidbodyKinematic(uint entityId) => _getRigidbodyKinematic != null && _getRigidbodyKinematic(entityId) != 0;
    internal static void SetRigidbodyKinematic(uint entityId, bool value) { if (_setRigidbodyKinematic != null) _setRigidbodyKinematic(entityId, value ? 1 : 0); }
    internal static bool GetRigidbodyFreezeRotation(uint entityId) => _getRigidbodyFreezeRotation != null && _getRigidbodyFreezeRotation(entityId) != 0;
    internal static void SetRigidbodyFreezeRotation(uint entityId, bool value) { if (_setRigidbodyFreezeRotation != null) _setRigidbodyFreezeRotation(entityId, value ? 1 : 0); }
    internal static Vector3 GetRigidbodyVelocity(uint entityId) => _getRigidbodyVelocity == null ? Vector3.Zero : _getRigidbodyVelocity(entityId).ToManaged();
    internal static void SetRigidbodyVelocity(uint entityId, Vector3 value) { if (_setRigidbodyVelocity != null) _setRigidbodyVelocity(entityId, NativeVector3.FromManaged(value)); }
    internal static Vector3 GetRigidbodyAngularVelocity(uint entityId) => _getRigidbodyAngularVelocity == null ? Vector3.Zero : _getRigidbodyAngularVelocity(entityId).ToManaged();
    internal static void SetRigidbodyAngularVelocity(uint entityId, Vector3 value) { if (_setRigidbodyAngularVelocity != null) _setRigidbodyAngularVelocity(entityId, NativeVector3.FromManaged(value)); }

    internal static int GetColliderShape(uint entityId) => _getColliderShape == null ? 0 : _getColliderShape(entityId);
    internal static void SetColliderShape(uint entityId, int value) { if (_setColliderShape != null) _setColliderShape(entityId, value); }
    internal static Vector3 GetColliderCenter(uint entityId) => _getColliderCenter == null ? Vector3.Zero : _getColliderCenter(entityId).ToManaged();
    internal static void SetColliderCenter(uint entityId, Vector3 value) { if (_setColliderCenter != null) _setColliderCenter(entityId, NativeVector3.FromManaged(value)); }
    internal static Vector3 GetColliderSize(uint entityId) => _getColliderSize == null ? Vector3.One : _getColliderSize(entityId).ToManaged();
    internal static void SetColliderSize(uint entityId, Vector3 value) { if (_setColliderSize != null) _setColliderSize(entityId, NativeVector3.FromManaged(value)); }
    internal static float GetColliderRadius(uint entityId) => _getColliderRadius == null ? 0.0f : _getColliderRadius(entityId);
    internal static void SetColliderRadius(uint entityId, float value) { if (_setColliderRadius != null) _setColliderRadius(entityId, value); }
    internal static float GetColliderHeight(uint entityId) => _getColliderHeight == null ? 0.0f : _getColliderHeight(entityId);
    internal static void SetColliderHeight(uint entityId, float value) { if (_setColliderHeight != null) _setColliderHeight(entityId, value); }
    internal static bool GetColliderTrigger(uint entityId) => _getColliderTrigger != null && _getColliderTrigger(entityId) != 0;
    internal static void SetColliderTrigger(uint entityId, bool value) { if (_setColliderTrigger != null) _setColliderTrigger(entityId, value ? 1 : 0); }

    internal static bool GetKeyDown(int keyCode)
    {
        return _getKeyDown != null && _getKeyDown(keyCode) != 0;
    }

    internal static bool GetKeyPressed(int keyCode)
    {
        return _getKeyPressed != null && _getKeyPressed(keyCode) != 0;
    }

    internal static bool GetKeyReleased(int keyCode)
    {
        return _getKeyReleased != null && _getKeyReleased(keyCode) != 0;
    }

    internal static bool GetMouseButtonDown(int button)
    {
        return _getMouseButtonDown != null && _getMouseButtonDown(button) != 0;
    }

    internal static bool GetMouseButtonPressed(int button)
    {
        return _getMouseButtonPressed != null && _getMouseButtonPressed(button) != 0;
    }

    internal static bool GetMouseButtonReleased(int button)
    {
        return _getMouseButtonReleased != null && _getMouseButtonReleased(button) != 0;
    }

    internal static Vector2 GetMousePosition()
    {
        if (_getMousePosition == null)
        {
            return Vector2.Zero;
        }

        var position = _getMousePosition();
        return new Vector2(position.X, position.Y);
    }

    internal static Vector2 GetMouseDelta()
    {
        if (_getMouseDelta == null)
        {
            return Vector2.Zero;
        }

        var delta = _getMouseDelta();
        return new Vector2(delta.X, delta.Y);
    }

    internal static Vector2 GetMouseScrollDelta()
    {
        if (_getMouseScrollDelta == null)
        {
            return Vector2.Zero;
        }

        var delta = _getMouseScrollDelta();
        return new Vector2(delta.X, delta.Y);
    }

    internal static bool GetQuitRequested()
    {
        return _getQuitRequested != null && _getQuitRequested() != 0;
    }

    internal static bool GetCursorLocked()
    {
        return _getCursorLocked != null && _getCursorLocked() != 0;
    }

    internal static void SetCursorLocked(bool locked)
    {
        if (_setCursorLocked == null)
        {
            return;
        }

        _setCursorLocked(locked ? 1 : 0);
    }

    private static void ResetLoadedAssembly()
    {
        Instances.Clear();
        ScriptClasses.Clear();

        if (_loadContext is null)
        {
            return;
        }

        _loadedAssembly = null;
        _loadContext.Unload();
        _loadContext = null;

        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static IEnumerable<ScriptClassMetadata> DiscoverScriptClasses(Assembly assembly)
    {
        foreach (var type in assembly.GetTypes())
        {
            if (type.IsAbstract || !typeof(ScriptBehaviour).IsAssignableFrom(type))
            {
                continue;
            }

            var defaultInstance = Activator.CreateInstance(type, nonPublic: true);
            var fields = new List<ScriptFieldMetadata>();

            foreach (var field in type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
            {
                if (field.IsStatic || field.GetCustomAttribute<SerializedFieldAttribute>() is null)
                {
                    continue;
                }

                var fieldType = MapFieldType(field.FieldType);
                if (fieldType is null)
                {
                    continue;
                }

                fields.Add(new ScriptFieldMetadata(field.Name, fieldType.Value, field.GetValue(defaultInstance), field));
            }

            foreach (var property in type.GetProperties(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
            {
                if (property.GetCustomAttribute<SerializedFieldAttribute>() is null || property.GetMethod is null || property.SetMethod is null)
                {
                    continue;
                }

                var fieldType = MapFieldType(property.PropertyType);
                if (fieldType is null)
                {
                    continue;
                }

                fields.Add(new ScriptFieldMetadata(property.Name, fieldType.Value, property.GetValue(defaultInstance), property));
            }

            yield return new ScriptClassMetadata(
                assembly.GetName().Name ?? string.Empty,
                type.Namespace ?? string.Empty,
                type.Name,
                type,
                fields);
        }
    }

    private static int? MapFieldType(Type type)
    {
        if (type == typeof(bool))
        {
            return 1;
        }

        if (type == typeof(int))
        {
            return 2;
        }

        if (type == typeof(float))
        {
            return 3;
        }

        if (type == typeof(double))
        {
            return 4;
        }

        if (type == typeof(string))
        {
            return 5;
        }

        if (type == typeof(Vector2))
        {
            return 6;
        }

        if (type == typeof(Vector3))
        {
            return 7;
        }

        if (type == typeof(uint))
        {
            return 8;
        }

        if (type == typeof(GameObject))
        {
            return 9;
        }

        if (type == typeof(MeshComponent))
        {
            return 10;
        }

        if (type == typeof(CameraComponent))
        {
            return 11;
        }

        if (type == typeof(LightComponent))
        {
            return 12;
        }

        if (type == typeof(RigidbodyComponent))
        {
            return 13;
        }

        if (type == typeof(ColliderComponent))
        {
            return 14;
        }

        return null;
    }

    private static string BuildMetadataPayload()
    {
        var builder = new StringBuilder();

        foreach (var scriptClass in ScriptClasses.Values)
        {
            builder.Append("CLASS\t")
                .Append(Escape(scriptClass.AssemblyName)).Append('\t')
                .Append(Escape(scriptClass.NamespaceName)).Append('\t')
                .Append(Escape(scriptClass.ClassName)).Append('\n');

            foreach (var field in scriptClass.Fields)
            {
                builder.Append("FIELD\t")
                    .Append(Escape(field.Name)).Append('\t')
                    .Append(field.Type.ToString(CultureInfo.InvariantCulture)).Append('\t')
                    .Append('1').Append('\t')
                    .Append(Escape(SerializeValue(field.Type, field.DefaultValue))).Append('\n');
            }

            builder.Append("END\n");
        }

        return builder.ToString();
    }

    private static void ApplyFieldValues(ScriptBehaviour instance, ScriptClassMetadata scriptClass, string fieldData)
    {
        foreach (var line in fieldData.Split('\n', StringSplitOptions.RemoveEmptyEntries))
        {
            var tokens = SplitEscaped(line, '\t');
            if (tokens.Count < 4 || !string.Equals(tokens[0], "FIELD", StringComparison.Ordinal))
            {
                continue;
            }

            var fieldName = tokens[1];
            var fieldType = int.Parse(tokens[2], CultureInfo.InvariantCulture);
            var member = scriptClass.Fields.FirstOrDefault(candidate => candidate.Name == fieldName);
            if (member is null)
            {
                continue;
            }

            var memberType = member.Member switch
            {
                FieldInfo fieldInfo => fieldInfo.FieldType,
                PropertyInfo propertyInfo => propertyInfo.PropertyType,
                _ => typeof(object),
            };
            var value = ParseValue(fieldType, tokens[3], memberType);

            if (member.Member is FieldInfo field)
            {
                field.SetValue(instance, value);
            }
            else if (member.Member is PropertyInfo property)
            {
                property.SetValue(instance, value);
            }
        }
    }

    private static string BuildFieldData(ScriptBehaviour instance, ScriptClassMetadata scriptClass)
    {
        var builder = new StringBuilder();

        foreach (var field in scriptClass.Fields)
        {
            var value = field.Member switch
            {
                FieldInfo fieldInfo => fieldInfo.GetValue(instance),
                PropertyInfo propertyInfo => propertyInfo.GetValue(instance),
                _ => null,
            };

            builder.Append("FIELD\t")
                .Append(Escape(field.Name)).Append('\t')
                .Append(field.Type.ToString(CultureInfo.InvariantCulture)).Append('\t')
                .Append(Escape(SerializeValue(field.Type, value))).Append('\n');
        }

        return builder.ToString();
    }

    private static object? ParseValue(int fieldType, string value, Type memberType)
    {
        return fieldType switch
        {
            1 => string.Equals(value, "true", StringComparison.Ordinal),
            2 => int.Parse(value, CultureInfo.InvariantCulture),
            3 => float.Parse(value, CultureInfo.InvariantCulture),
            4 => double.Parse(value, CultureInfo.InvariantCulture),
            5 => value,
            6 => ParseVector2(value),
            7 => ParseVector3(value),
            8 => uint.Parse(value, CultureInfo.InvariantCulture),
            9 => CreateReferenceValue(memberType, value),
            10 => CreateReferenceValue(memberType, value),
            11 => CreateReferenceValue(memberType, value),
            12 => CreateReferenceValue(memberType, value),
            13 => CreateReferenceValue(memberType, value),
            14 => CreateReferenceValue(memberType, value),
            _ => null,
        };
    }

    private static string SerializeValue(int fieldType, object? value)
    {
        return fieldType switch
        {
            1 => (bool?)value == true ? "true" : "false",
            2 => Convert.ToInt32(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
            3 => Convert.ToSingle(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
            4 => Convert.ToDouble(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
            5 => value as string ?? string.Empty,
            6 => SerializeVector2(value),
            7 => SerializeVector3(value),
            8 => Convert.ToUInt32(value, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture),
            9 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            10 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            11 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            12 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            13 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            14 => ExtractEntityId(value).ToString(CultureInfo.InvariantCulture),
            _ => string.Empty,
        };
    }

    private static object? CreateReferenceValue(Type memberType, string value)
    {
        var entityId = uint.Parse(value, CultureInfo.InvariantCulture);
        if (entityId == 0)
        {
            return null;
        }

        if (memberType == typeof(GameObject))
        {
            return new GameObject(entityId);
        }

        if (memberType == typeof(MeshComponent))
        {
            return new MeshComponent(entityId);
        }

        if (memberType == typeof(CameraComponent))
        {
            return new CameraComponent(entityId);
        }

        if (memberType == typeof(LightComponent))
        {
            return new LightComponent(entityId);
        }

        if (memberType == typeof(RigidbodyComponent))
        {
            return new RigidbodyComponent(entityId);
        }

        if (memberType == typeof(ColliderComponent))
        {
            return new ColliderComponent(entityId);
        }

        return entityId;
    }

    private static uint ExtractEntityId(object? value)
    {
        return value switch
        {
            null => 0u,
            uint entityId => entityId,
            GameObject gameObject => gameObject.EntityId,
            MeshComponent meshComponent => meshComponent.EntityId,
            CameraComponent cameraComponent => cameraComponent.EntityId,
            LightComponent lightComponent => lightComponent.EntityId,
            RigidbodyComponent rigidbodyComponent => rigidbodyComponent.EntityId,
            ColliderComponent colliderComponent => colliderComponent.EntityId,
            _ => 0u,
        };
    }

    private static string SerializeVector2(object? value)
    {
        var vector = value is Vector2 typedValue ? typedValue : Vector2.Zero;
        return string.Create(CultureInfo.InvariantCulture, $"{vector.X},{vector.Y}");
    }

    private static string SerializeVector3(object? value)
    {
        var vector = value is Vector3 typedValue ? typedValue : Vector3.Zero;
        return string.Create(CultureInfo.InvariantCulture, $"{vector.X},{vector.Y},{vector.Z}");
    }

    private static Vector2 ParseVector2(string value)
    {
        var parts = SplitEscaped(value, ',');
        return parts.Count == 2
            ? new Vector2(float.Parse(parts[0], CultureInfo.InvariantCulture), float.Parse(parts[1], CultureInfo.InvariantCulture))
            : Vector2.Zero;
    }

    private static Vector3 ParseVector3(string value)
    {
        var parts = SplitEscaped(value, ',');
        return parts.Count == 3
            ? new Vector3(float.Parse(parts[0], CultureInfo.InvariantCulture), float.Parse(parts[1], CultureInfo.InvariantCulture), float.Parse(parts[2], CultureInfo.InvariantCulture))
            : Vector3.Zero;
    }

    private static List<string> SplitEscaped(string text, char delimiter)
    {
        var tokens = new List<string>();
        var builder = new StringBuilder();
        var escaping = false;

        foreach (var character in text)
        {
            if (escaping)
            {
                builder.Append(character switch
                {
                    'n' => '\n',
                    't' => '\t',
                    _ => character,
                });

                escaping = false;
                continue;
            }

            if (character == '\\')
            {
                escaping = true;
                continue;
            }

            if (character == delimiter)
            {
                tokens.Add(builder.ToString());
                builder.Clear();
                continue;
            }

            builder.Append(character);
        }

        tokens.Add(builder.ToString());
        return tokens;
    }

    private static string Escape(string value)
    {
        return value
            .Replace("\\", "\\\\", StringComparison.Ordinal)
            .Replace("\t", "\\t", StringComparison.Ordinal)
            .Replace("\n", "\\n", StringComparison.Ordinal);
    }

    private static void SetError(string message)
    {
        lock (Gate)
        {
            _lastError = message;
        }
    }
}
