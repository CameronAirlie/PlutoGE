# PlutoGE game developer documentation

Use this manual to build games with the editor and C# gameplay API. Start with a
small playable scene, then add systems one at a time. Instructions describe the
source in this checkout; PlutoGE is under development and not every native
component has a C# wrapper. Inspector recipes below use authored components;
code examples use the public `PlutoGE.ScriptCore` API.

## Learning path

| Guide | What you will build or learn |
| --- | --- |
| [First playable game](guide/first-game.md) | Create a project, assemble a room, move a character, and test a game camera |
| [Editor, assets, and worlds](guide/world-building.md) | Work with scenes, imports, prefabs, terrain, foliage, splines, and navigation |
| [Gameplay and physics](guide/gameplay.md) | Write behaviours, map input, simulate bodies, query collisions, spawn prefabs, and animate characters |
| [Rendering, effects, and audio](guide/presentation.md) | Author materials, lighting, skies, water, particles, decals, cloth, and sound |
| [UI, saves, networking, and shipping](guide/game-systems.md) | Build interfaces, change scenes, persist progress, connect players, profile, and export |

The examples are intentionally small. Each complete C# class belongs in its own
file under your game's `Assets/Scripts`, followed by **Runtime > Build Scripts**.
Attach it with a Script component and assign its serialized references in the
Inspector. Other code blocks are explicitly identified as fragments.

## Feature references

| Area | Detailed reference |
| --- | --- |
| Engine build and requirements | [Repository README](../README.md#build-and-run) |
| Editor iteration, recovery, validation, debug drawing | [Editor workflows](EDITOR_WORKFLOWS.md) |
| Editing several entities | [Multi-entity editing](MULTI_ENTITY_EDITING.md) |
| Reusable entity variations | [Prefab variants](PREFAB_VARIANTS.md) |
| Managed API and serialization | [C# scripting](CSHARP_SCRIPTING.md) |
| Character and vehicle cameras | [Camera rigs](CAMERA_RIGS.md) |
| Physics-driven characters | [Ragdolls](RAGDOLLS.md) |
| Contact sounds and effects | [Surface responses](SURFACE_RESPONSES.md) |
| Instanced vegetation and collision | [Foliage](FOLIAGE.md) |
| Surface marks | [Decals](decals.md) |
| Document UI | [RmlUi quick start](RMLUI_QUICKSTART.md), [integration](RMLUI_INTEGRATION.md) |
| Multiplayer transport | [Networking](NETWORKING.md) |
| Measuring performance | [Profiler](PROFILER.md) |
| Upscaling | [FSR 2](FSR2.md), [DLSS](DLSS.md) |
| Player builds | [Exporting](../EXPORTING.md) |

Renderer implementation notes and milestone plans also live in `docs/`. They are
engineering references, not promises that every described or planned feature is
available in a game build. For a rendering issue, record the backend, build
configuration, GPU, scene, and effect settings when comparing results.
