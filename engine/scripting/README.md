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
