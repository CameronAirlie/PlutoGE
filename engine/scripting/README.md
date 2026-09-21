# PlutoGE Scripting

PlutoGE executes .NET 8 C# scripts through its `hostfxr` runtime. The scripting
system includes managed project compilation, assembly loading and reflection,
editor-exposed serialized fields, entity-attached behaviours, scriptable-object
data assets, and native engine API wrappers.

For the complete public C# API and AI authoring rules, see
[`docs/CSHARP_SCRIPTING.md`](../../docs/CSHARP_SCRIPTING.md).

The implementation is split into:

1. `ScriptEngine`, which builds projects and owns runtime/class registration.
2. `HostFxrScriptRuntime`, which hosts .NET and communicates through the managed bridge.
3. `ScriptComponent`, which stores the selected class and per-entity field values and invokes its lifecycle.
4. `PlutoGE.ScriptCore`, which provides the public C# gameplay API.

The managed API includes fixed-step callbacks and input action maps, camera rigs,
surface responses, native/RmlUi UI controls, additive scene-section handles, and
network entity replication. Use the focused references for newer systems:

- [Camera rigs](../../docs/CAMERA_RIGS.md) and [surface responses](../../docs/SURFACE_RESPONSES.md)
- [Scene streaming](../../docs/SCENE_STREAMING.md)
- [Transport](../../docs/NETWORKING.md) and [entity replication](../../docs/ENTITY_REPLICATION.md)
- [RmlUi](../../docs/RMLUI_QUICKSTART.md)

Native components do not all have managed wrappers; in particular, use the
documented native/editor Sequencer controls rather than assuming a C# wrapper.
