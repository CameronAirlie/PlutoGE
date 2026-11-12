# PlutoGE game developer documentation

Use this manual to build games with the editor and C# gameplay API. Start with a
small playable scene, then add systems one at a time. Instructions describe the
source in this checkout; PlutoGE is under development and not every native
component has a C# wrapper. Inspector recipes below use authored components;
code examples use the public `PlutoGE.ScriptCore` API.

## Learning path

| Guide | What you will build or learn |
| --- | --- |
| [First playable game](guide/first-game.md) | Start from a template, play it, tune it, add gameplay kits and write your first script |
| [Editor, assets, and worlds](guide/world-building.md) | Work with scenes, imports, prefabs, terrain, foliage, splines, and navigation |
| [Gameplay and physics](guide/gameplay.md) | Write behaviours, map input, simulate bodies, query collisions, spawn prefabs, and animate characters |
| [Rendering, effects, and audio](guide/presentation.md) | Author materials, lighting, skies, water, particles, decals, cloth, and sound |
| [UI, saves, networking, and shipping](guide/game-systems.md) | Build interfaces, change scenes, persist progress, connect players, profile, and export |

The examples are intentionally small. Each complete C# class belongs in its own
file under your game's `Assets/Scripts`, followed by **Scripts > Build Scripts**.
Attach it with a Script component and assign its serialized references in the
Inspector. Other code blocks are explicitly identified as fragments.

## Feature references

| Area | Detailed reference |
| --- | --- |
| Engine build and requirements | [Repository README](../README.md#build-and-run) |
| Current renderer support and limitations | [Rendering support](RENDERING.md) |
| Entity-based UI: layout, controls, styles, designer, scripting | [Native UI](NATIVE_UI.md) |
| Editor iteration, shortcuts, command palette, recovery, validation, debug drawing | [Editor workflows](EDITOR_WORKFLOWS.md) |
| Project templates, gameplay kits, visual UI builder, save games | [Faster authoring workflows](EDITOR_AUTHORING.md) |
| Finding, importing and organising assets | [Content browser](CONTENT_BROWSER.md) |
| Creating, parenting and filtering entities | [Scene hierarchy](SCENE_HIERARCHY.md) |
| Editor title bars and detached windows | [Editor window chrome](EDITOR_WINDOW_CHROME.md) |
| Editing several entities | [Multi-entity editing](MULTI_ENTITY_EDITING.md) |
| Reusable entity variations | [Prefab variants](PREFAB_VARIANTS.md) |
| Timeline authoring and playback | [Sequencer](SEQUENCER.md) |
| Additive worlds and section lifetime | [Scene streaming](SCENE_STREAMING.md) |
| Banked roads, junctions and roadside bakes | [Spline roads](SPLINE_ROADS.md) |
| Managed API and serialization | [C# scripting](CSHARP_SCRIPTING.md) |
| Character and vehicle cameras | [Camera rigs](CAMERA_RIGS.md) |
| Orthographic game cameras | [Projection, framing and zoom](ORTHOGRAPHIC_CAMERAS.md) |
| Weapon and overlay cameras | [Camera stacking](CAMERA_STACKING.md) |
| Monitors, mirrors and in-world screens | [Render textures](RENDER_TEXTURES.md) |
| Character animation state machines and blending | [Animation graphs](ANIMATION_GRAPHS.md) |
| Two-bone IK and props attached to bones | [Skeletal IK and attachments](SKELETAL_IK.md) |
| First-person bodies (joints hidden from cameras, shadow kept) | [Skeletal IK: hiding joints](SKELETAL_IK.md#hiding-joints-from-cameras) |
| Physics-driven characters | [Ragdolls](RAGDOLLS.md) |
| Contact sounds and effects | [Surface responses](SURFACE_RESPONSES.md) |
| Instanced vegetation and collision | [Foliage](FOLIAGE.md) |
| Wind and swaying vegetation | [Foliage wind](FOLIAGE_WIND.md) |
| Light through leaves and canopies, on voxel and hardware GI | [Foliage transmission](FOLIAGE_TRANSMISSION.md) |
| Surface marks | [Decals](decals.md) |
| Highlighting what the player can interact with | [Outlines](OUTLINES.md) |
| Directional shadows | [Virtual shadow maps](virtual-shadow-maps.md), [defaults and compatibility](VSM_DEFAULT.md), [cascade caching and caster culling](cascaded-shadow-caching.md) |
| Clouds and weather | [Cloud shadows, overcast light and cloud rendering](CLOUD_SHADOWS.md) |
| Local lights and shadows | [Physical lights](physical-local-lights.md), [point shadow filtering](point-shadow-filtering.md) |
| Surface lighting | [BRDF, area lights, specular occlusion, contact shadows, tone mapping](SURFACE_LIGHTING.md) |
| Baked global illumination (low-end GPUs) | [Ambient probe grids](ambient-probe-grids.md), [live sky, reflection occlusion and capture relighting](BAKED_ENVIRONMENT_LIGHTING.md) |
| Dynamic global illumination | [DDGI probes](DDGI.md), [screen-space GI](SSGI.md) |
| Voxel global illumination | [World cache](vct-world-cache.md), [secondary bounce](vct-secondary-bounce.md) |
| Water | [Ocean waves, foam, ripples and caustics](OCEAN_WATER_EFFECTS.md) |
| Stylized rendering | [Toon shaders](ToonShaders.md), [outlines](OutlineShaders.md) |
| Document UI | [RML authoring guide for developers and agents](RMLUI_AUTHORING.md), [feature reference](RMLUI_REFERENCE.md), [quick start](RMLUI_QUICKSTART.md), [integration](RMLUI_INTEGRATION.md) |
| The player's master volume | [Audio output volume](AudioOutputVolume.md) |
| Gamepad rumble | [Controller vibration](ControllerVibration.md) |
| Multiplayer transport | [Networking](NETWORKING.md) |
| Multiplayer entity state and sessions | [Entity replication](ENTITY_REPLICATION.md) |
| Dedicated servers without a window | [Headless servers](HEADLESS_SERVERS.md) |
| Loading screens and scene transitions | [Scene loading](scene-loading.md) |
| Measuring performance | [Profiler](PROFILER.md) |
| Frame rate limit, delta time and mouse input pacing | [Frame pacing](FRAME_PACING.md) |
| Rendering diagnostics | [Geometry profiling](GEOMETRY_PROFILING.md), [occlusion culling](OCCLUSION_CULLING.md) |
| Player window modes | [Fullscreen and window settings](window-fullscreen.md) |
| Quality levels players can choose | [Graphics presets](GraphicsPresets.md) |
| Cooked asset containers | [Content packs](CONTENT_PACKS.md) |
| Anti-aliasing and upscaling | [TAA](TAA.md), [FSR 2](FSR2.md), [DLSS](DLSS.md) |
| Player builds | [Exporting](../EXPORTING.md) |

Engine implementation notes, plans and performance investigations are collected
in [engineering notes](dev/README.md). They are engineering references, not
promises that every described or planned feature is available in a game build. For a rendering issue, record the backend, build
configuration, GPU, scene, and effect settings when comparing results.
