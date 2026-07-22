using System.Collections.ObjectModel;
using System.Runtime.InteropServices;

namespace PlutoGE.Editor.Avalonia.Native;

internal sealed unsafe class EngineHost : IDisposable
{
    private readonly object _sync = new();
    private readonly HashSet<ulong> _viewports = [];
    private ulong _engine;
    private string? _loadedProjectPath;
    private string? _loadedScenePath;
    private bool _disposed;

    internal event EventHandler? EngineReady;
    internal event EventHandler<string>? StatusChanged;
    internal bool IsReady => _engine != 0;

    internal ulong AcquireViewport(PlutoNative.GlProcAddress resolver, int width, int height)
    {
        lock (_sync)
        {
            ObjectDisposedException.ThrowIf(_disposed, this);
            if (_engine == 0)
            {
                if (PlutoNative.ApiVersionNative() != PlutoNative.ApiVersion)
                    throw new InvalidOperationException("The PlutoGE editor native DLL uses an incompatible API version.");
                var config = new PlutoNative.EngineConfig
                {
                    StructSize = (uint)sizeof(PlutoNative.EngineConfig),
                    ApiVersion = PlutoNative.ApiVersion,
                    InitialWidth = Math.Max(width, 1),
                    InitialHeight = Math.Max(height, 1),
                    GetProcAddress = Marshal.GetFunctionPointerForDelegate(resolver),
                };
                PlutoNative.ThrowIfFailed(PlutoNative.EngineCreate(in config, out _engine), "Engine creation");
                if (!string.IsNullOrWhiteSpace(_loadedProjectPath))
                    PlutoNative.ThrowIfFailed(PlutoNative.ProjectLoad(_engine, _loadedProjectPath), "Restoring project");
                if (!string.IsNullOrWhiteSpace(_loadedScenePath))
                    PlutoNative.ThrowIfFailed(PlutoNative.SceneLoad(_engine, _loadedScenePath), "Restoring scene");
                Console.Error.WriteLine("PlutoGE native engine initialized in the Avalonia OpenGL context.");
                EngineReady?.Invoke(this, EventArgs.Empty);
            }

            PlutoNative.ThrowIfFailed(PlutoNative.ViewportCreate(_engine, out var viewport), "Viewport creation");
            _viewports.Add(viewport);
            return viewport;
        }
    }

    internal void ReleaseViewport(ulong viewport)
    {
        lock (_sync)
        {
            if (_engine == 0 || !_viewports.Remove(viewport)) return;
            PlutoNative.ViewportDestroy(_engine, viewport);
            if (_viewports.Count == 0)
            {
                PlutoNative.EngineDestroy(_engine);
                _engine = 0;
            }
        }
    }

    internal PlutoNative.Result Render(ulong viewport, uint selectedEntityId, in PlutoNative.ViewportFrame frame, out bool gizmoActive)
    {
        lock (_sync)
        {
            gizmoActive = false;
            if (_engine == 0) return PlutoNative.Result.InvalidHandle;
            PlutoNative.ViewportSetSelectedEntity(_engine, viewport, selectedEntityId);
            var result = PlutoNative.ViewportRender(_engine, viewport, in frame);
            if (result == PlutoNative.Result.Ok &&
                PlutoNative.ViewportGetGizmoActive(_engine, viewport, out var active) == PlutoNative.Result.Ok)
            {
                gizmoActive = active != 0;
            }
            return result;
        }
    }

    internal void SetGizmoOperation(ulong viewport, int operation)
    {
        lock (_sync)
            if (_engine != 0) PlutoNative.ViewportSetGizmoOperation(_engine, viewport, operation);
    }

    internal bool TryPickEntity(ulong viewport, float mouseX, float mouseY, out uint entityId)
    {
        lock (_sync)
        {
            entityId = 0;
            return _engine != 0 &&
                   PlutoNative.ViewportPickEntity(_engine, viewport, mouseX, mouseY, out entityId) == PlutoNative.Result.Ok;
        }
    }

    internal IReadOnlyList<EntityNode> ReadHierarchy()
    {
        lock (_sync)
        {
            if (_engine == 0 || PlutoNative.SceneGetEntityCount(_engine, out var count) != PlutoNative.Result.Ok)
                return [];

            var nodes = new Dictionary<uint, EntityNode>();
            var ordered = new List<EntityNode>((int)count);
            for (uint index = 0; index < count; ++index)
            {
                PlutoNative.ThrowIfFailed(PlutoNative.SceneGetEntity(_engine, index, out var entity), "Reading hierarchy");
                var node = new EntityNode(entity.Id, entity.ParentId, entity.GetName(), entity.Active != 0);
                nodes.Add(node.Id, node);
                ordered.Add(node);
            }

            var roots = new List<EntityNode>();
            foreach (var node in ordered)
            {
                if (node.ParentId != 0 && nodes.TryGetValue(node.ParentId, out var parent)) parent.Children.Add(node);
                else roots.Add(node);
            }
            return roots;
        }
    }

    internal EntityTransform? ReadTransform(uint entityId)
    {
        lock (_sync)
        {
            if (_engine == 0 || PlutoNative.EntityGetTransform(_engine, entityId, out var value) != PlutoNative.Result.Ok)
                return null;
            return new EntityTransform(
                value.Position[0], value.Position[1], value.Position[2],
                value.Rotation[0], value.Rotation[1], value.Rotation[2],
                value.Scale[0], value.Scale[1], value.Scale[2]);
        }
    }

    internal EntityTransform WriteTransform(uint entityId, EntityTransform value)
    {
        lock (_sync)
        {
            if (_engine == 0) throw new InvalidOperationException("The native engine is not ready.");
            PlutoNative.Transform native = default;
            native.Position[0] = value.PositionX; native.Position[1] = value.PositionY; native.Position[2] = value.PositionZ;
            native.Rotation[0] = value.RotationX; native.Rotation[1] = value.RotationY; native.Rotation[2] = value.RotationZ;
            native.Scale[0] = value.ScaleX; native.Scale[1] = value.ScaleY; native.Scale[2] = value.ScaleZ;
            PlutoNative.ThrowIfFailed(PlutoNative.EntitySetTransform(_engine, entityId, in native), "Writing transform");
            PlutoNative.ThrowIfFailed(PlutoNative.EntityGetTransform(_engine, entityId, out var committed), "Verifying transform");
            return new EntityTransform(
                committed.Position[0], committed.Position[1], committed.Position[2],
                committed.Rotation[0], committed.Rotation[1], committed.Rotation[2],
                committed.Scale[0], committed.Scale[1], committed.Scale[2]);
        }
    }

    internal ProjectDocument LoadProject(string manifestPath)
    {
        lock (_sync)
        {
            EnsureReady();
            PlutoNative.ThrowIfFailed(PlutoNative.ProjectLoad(_engine, manifestPath), "Loading project");
            _loadedProjectPath = manifestPath;
            _loadedScenePath = null;
            return ReadProjectCore();
        }
    }

    internal ProjectDocument? ReadProject()
    {
        lock (_sync)
        {
            if (_engine == 0 || PlutoNative.ProjectGetInfo(_engine, out _) != PlutoNative.Result.Ok)
                return null;
            return ReadProjectCore();
        }
    }

    private ProjectDocument ReadProjectCore()
    {
        PlutoNative.ThrowIfFailed(PlutoNative.ProjectGetInfo(_engine, out var info), "Reading project");
        PlutoNative.ThrowIfFailed(PlutoNative.ProjectGetAssetCount(_engine, out var count), "Reading project assets");
        var assets = new List<ProjectAsset>((int)count);
        for (uint index = 0; index < count; ++index)
        {
            PlutoNative.ThrowIfFailed(PlutoNative.ProjectGetAsset(_engine, index, out var asset), "Reading project asset");
            assets.Add(new ProjectAsset(asset.GetReference(), asset.Type.ToString(), asset.Type == PlutoNative.ProjectAssetType.Scene, asset.Size));
        }
        return new ProjectDocument(info.GetName(), info.GetManifestPath(), info.GetAssetDirectory(), info.GetStartupScene(), assets);
    }

    internal string LoadScene(string pathOrReference)
    {
        lock (_sync)
        {
            EnsureReady();
            PlutoNative.ThrowIfFailed(PlutoNative.SceneLoad(_engine, pathOrReference), "Loading scene");
            PlutoNative.ThrowIfFailed(PlutoNative.SceneGetInfo(_engine, out var scene), "Reading scene");
            _loadedScenePath = scene.GetPath();
            return _loadedScenePath;
        }
    }

    internal string ReadScenePath()
    {
        lock (_sync)
            return _engine != 0 && PlutoNative.SceneGetInfo(_engine, out var scene) == PlutoNative.Result.Ok
                ? scene.GetPath()
                : string.Empty;
    }

    internal IReadOnlyList<EntityComponent> ReadComponents(uint entityId)
    {
        lock (_sync)
        {
            if (_engine == 0 || PlutoNative.EntityGetComponentCount(_engine, entityId, out var count) != PlutoNative.Result.Ok)
                return [];
            var components = new List<EntityComponent>((int)count);
            for (uint componentIndex = 0; componentIndex < count; ++componentIndex)
            {
                PlutoNative.ThrowIfFailed(PlutoNative.EntityGetComponent(_engine, entityId, componentIndex, out var component), "Reading component");
                PlutoNative.ThrowIfFailed(PlutoNative.ComponentGetPropertyCount(_engine, entityId, componentIndex, out var propertyCount), "Reading component properties");
                var properties = new List<ComponentPropertyValue>((int)propertyCount);
                for (uint propertyIndex = 0; propertyIndex < propertyCount; ++propertyIndex)
                {
                    PlutoNative.ThrowIfFailed(PlutoNative.ComponentGetProperty(_engine, entityId, componentIndex, propertyIndex, out var property), "Reading component property");
                    properties.Add(new ComponentPropertyValue(
                        propertyIndex, property.GetName(), property.GetValue(), property.Type,
                        property.Editable != 0, property.GetEnumOptions()));
                }
                components.Add(new EntityComponent(component.Index, component.GetName(), component.Enabled != 0, properties));
            }
            return components;
        }
    }

    internal void WriteComponentEnabled(uint entityId, uint componentIndex, bool enabled)
    {
        lock (_sync)
        {
            EnsureReady();
            PlutoNative.ThrowIfFailed(
                PlutoNative.ComponentSetEnabled(_engine, entityId, componentIndex, enabled ? (byte)1 : (byte)0),
                "Updating component state");
        }
    }

    internal void WriteComponentProperty(uint entityId, uint componentIndex, uint propertyIndex, string value)
    {
        lock (_sync)
        {
            EnsureReady();
            PlutoNative.ThrowIfFailed(
                PlutoNative.ComponentSetProperty(_engine, entityId, componentIndex, propertyIndex, value),
                "Updating component property");
        }
    }

    internal IReadOnlyList<string> ReadRegisteredPostProcessTypes()
    {
        lock (_sync)
        {
            EnsureReady();
            PlutoNative.ThrowIfFailed(PlutoNative.PostProcessGetRegisteredTypeCount(_engine, out var count), "Reading post-process effect types");
            var types = new List<string>((int)count);
            Span<byte> buffer = stackalloc byte[120];
            for (uint index = 0; index < count; ++index)
            {
                buffer.Clear();
                fixed (byte* value = buffer)
                {
                    PlutoNative.ThrowIfFailed(
                        PlutoNative.PostProcessGetRegisteredType(_engine, index, value, (uint)buffer.Length),
                        "Reading a post-process effect type");
                    types.Add(System.Text.Encoding.UTF8.GetString(buffer[..buffer.IndexOf((byte)0)]));
                }
            }
            return types;
        }
    }

    internal IReadOnlyList<EditorCameraPostProcessEffect> ReadEditorCameraPostProcessEffects()
    {
        lock (_sync)
        {
            EnsureReady();
            PlutoNative.ThrowIfFailed(
                PlutoNative.EditorCameraGetPostProcessEffectCount(_engine, out var effectCount),
                "Reading editor-camera post processing");
            var effects = new List<EditorCameraPostProcessEffect>((int)effectCount);
            for (uint effectIndex = 0; effectIndex < effectCount; ++effectIndex)
            {
                PlutoNative.ThrowIfFailed(
                    PlutoNative.EditorCameraGetPostProcessEffect(_engine, effectIndex, out var effect),
                    "Reading a post-process effect");
                PlutoNative.ThrowIfFailed(
                    PlutoNative.EditorCameraGetPostProcessParameterCount(_engine, effectIndex, out var parameterCount),
                    "Reading post-process parameters");
                var parameters = new List<PostProcessParameterValue>((int)parameterCount);
                for (uint parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex)
                {
                    PlutoNative.ThrowIfFailed(
                        PlutoNative.EditorCameraGetPostProcessParameter(_engine, effectIndex, parameterIndex, out var parameter),
                        "Reading a post-process parameter");
                    parameters.Add(new PostProcessParameterValue(
                        parameterIndex, parameter.GetName(), parameter.GetValue(), parameter.Type, parameter.GetEnumOptions()));
                }
                effects.Add(new EditorCameraPostProcessEffect(
                    effect.Index, effect.GetTypeName(), effect.GetDisplayName(), effect.Enabled != 0, parameters));
            }
            return effects;
        }
    }

    internal void AddEditorCameraPostProcessEffect(string typeName) => InvokePostProcess(
        () => PlutoNative.EditorCameraAddPostProcessEffect(_engine, typeName), "Adding a post-process effect");

    internal void RemoveEditorCameraPostProcessEffect(uint effectIndex) => InvokePostProcess(
        () => PlutoNative.EditorCameraRemovePostProcessEffect(_engine, effectIndex), "Removing a post-process effect");

    internal void MoveEditorCameraPostProcessEffect(uint fromIndex, uint toIndex) => InvokePostProcess(
        () => PlutoNative.EditorCameraMovePostProcessEffect(_engine, fromIndex, toIndex), "Reordering post-process effects");

    internal void SetEditorCameraPostProcessEffectEnabled(uint effectIndex, bool enabled) => InvokePostProcess(
        () => PlutoNative.EditorCameraSetPostProcessEffectEnabled(_engine, effectIndex, enabled ? (byte)1 : (byte)0),
        "Updating a post-process effect");

    internal void SetEditorCameraPostProcessParameter(uint effectIndex, uint parameterIndex, string value) => InvokePostProcess(
        () => PlutoNative.EditorCameraSetPostProcessParameter(_engine, effectIndex, parameterIndex, value),
        "Updating a post-process parameter");

    private void InvokePostProcess(Func<PlutoNative.Result> action, string operation)
    {
        lock (_sync)
        {
            EnsureReady();
            PlutoNative.ThrowIfFailed(action(), operation);
        }
    }

    internal PlutoNative.FrameStats? GetStats(ulong viewport)
    {
        lock (_sync)
            return _engine != 0 && PlutoNative.ViewportGetStats(_engine, viewport, out var stats) == PlutoNative.Result.Ok ? stats : null;
    }

    internal void ReportStatus(string status)
    {
        Console.Error.WriteLine(status);
        StatusChanged?.Invoke(this, status);
    }

    private void EnsureReady()
    {
        if (_engine == 0)
            throw new InvalidOperationException("The engine is not ready yet. Open the Viewport window first.");
    }

    public void Dispose()
    {
        lock (_sync)
        {
            if (_disposed) return;
            _disposed = true;
            // GL-backed objects are normally released from OnOpenGlDeinit.
            // Do not destroy a still-live engine here without a current context.
        }
    }
}

internal sealed record EntityNode(uint Id, uint ParentId, string Name, bool Active)
{
    public ObservableCollection<EntityNode> Children { get; } = [];
}

internal readonly record struct EntityTransform(
    float PositionX, float PositionY, float PositionZ,
    float RotationX, float RotationY, float RotationZ,
    float ScaleX, float ScaleY, float ScaleZ);

internal sealed record ProjectDocument(
    string Name,
    string ManifestPath,
    string AssetDirectory,
    string StartupScene,
    IReadOnlyList<ProjectAsset> Assets);

internal sealed record ProjectAsset(string Reference, string Type, bool IsScene, ulong Size);

internal sealed record EntityComponent(
    uint Index,
    string Name,
    bool Enabled,
    IReadOnlyList<ComponentPropertyValue> Properties);

internal sealed record ComponentPropertyValue(
    uint Index,
    string Name,
    string Value,
    int Type,
    bool Editable,
    IReadOnlyList<string> EnumOptions);

internal sealed record EditorCameraPostProcessEffect(
    uint Index,
    string TypeName,
    string DisplayName,
    bool Enabled,
    IReadOnlyList<PostProcessParameterValue> Parameters);

internal sealed record PostProcessParameterValue(
    uint Index,
    string Name,
    string Value,
    int Type,
    IReadOnlyList<string> EnumOptions);
