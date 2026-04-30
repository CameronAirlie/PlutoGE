# PlutoGE Scripting

## Outline

The scripting system is split into three layers:

1. `PlutoGE::scripting::ScriptEngine` owns script class registration, managed project builds, and runtime loading.
2. `PlutoGE::scene::ScriptComponent` stores the selected script class plus serialized field values on an entity, then creates and ticks the script instance during `Scene::Update`.
3. A future managed runtime implementation of `IScriptRuntime` hosts the compiled C# assembly and materializes `ScriptInstance` objects from reflected C# types.

## Unity-style serialized fields

`ScriptClassDefinition` and `ScriptFieldDefinition` model the same data the editor needs to expose Unity-like serialized fields:

- `scriptClass`: the fully-qualified managed class name, such as `Game.PlayerController`
- `fields`: the declared serialized members exposed by the script type
- `fieldValues`: per-entity overrides stored by `ScriptComponent`

When a `ScriptComponent` resolves its class, it backfills any missing serialized values from the field defaults and applies them to the script instance before `OnCreate` runs.

## Current state

The native side now supports:

- a dedicated `engine/scripting` CMake library
- managed project compilation through `dotnet build`
- script class and serialized field registration
- entity-attached `ScriptComponent` instances that participate in the engine update loop

What is still intentionally pluggable is the CLR host. To execute compiled C# code, the next step is to add an `IScriptRuntime` implementation backed by either `hostfxr` or Mono embedding and populate `ScriptClassDefinition` data via reflection.