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
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3> _getEntityRotation;
    private static delegate* unmanaged[Cdecl]<uint, NativeVector3, void> _setEntityRotation;

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

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterEntityTransformApi")]
    public static int RegisterEntityTransformApi(delegate* unmanaged[Cdecl]<uint, NativeVector3> getEntityRotation, delegate* unmanaged[Cdecl]<uint, NativeVector3, void> setEntityRotation)
    {
        if (getEntityRotation == null || setEntityRotation == null)
        {
            SetError("Managed transform API registration received a null function pointer.");
            return 0;
        }

        _getEntityRotation = getEntityRotation;
        _setEntityRotation = setEntityRotation;
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
            var value = ParseValue(fieldType, tokens[3]);

            var member = scriptClass.Fields.FirstOrDefault(candidate => candidate.Name == fieldName);
            if (member is null)
            {
                continue;
            }

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

    private static object? ParseValue(int fieldType, string value)
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
            _ => string.Empty,
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