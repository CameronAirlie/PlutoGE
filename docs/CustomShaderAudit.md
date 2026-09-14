# Custom shader audit and implementation

The previous graph only generated legacy geometry GLSL. The OpenGL/Vulkan RHI scene translator copied fixed material properties and ignored the graph. Graph variables were serialized but could not be referenced by nodes, were not bound, and were lost when the material editor saved. Invalid links/cycles could fall back silently; scalar/vector expressions were emitted without checking dimensions. Unlit behavior was tied to one builtin reference.

## Implemented

- A validated, topologically ordered surface program shared by legacy GLSL and OpenGL/Vulkan RHI. Existing nodes now execute in the RHI renderer, including instanced draws and transparent surface draws. Output controls albedo, world-space normal, metallic, roughness, opacity and emission.
- Named Float, Vec2, Vec3 and Color parameters with defaults, Parameter nodes and explicit per-material overrides. Save preserves overrides; graph saves refresh loaded materials. Switching shaders retains unmatched overrides without applying them to unrelated parameters. A changed parameter type requires resetting that override.
- Unlit mode on any graph asset.
- Vec2, Vec3 and Color nodes expose both packed and scalar outputs. Enable Component Pins to combine scalar inputs; disable it to split a packed input. Existing links keep their pin names.
- Time (monotonic seconds since first shader-time query), World Position, World Normal, View Direction, Dot, Sine, Power and One Minus nodes. Time currently keeps running when gameplay is paused.
- Material Texture Sample nodes sample the material's albedo, normal, metallic or roughness texture at a supplied Vec2 UV. Color and individual RGBA outputs are available. Raw normal samples still need remapping from 0..1 to -1..1 and conversion to world space before connecting to Normal.
- Deterministic compilation and parameter hashing; batching separates different programs and overrides. Parameter changes keep register layout stable.
- Validation of IDs, pins, duplicate connections, cycles, output count, parameter references/types, finite values and connected vector dimensions. Scalars broadcast; different vector widths require an explicit conversion using the existing vector/component nodes. Division retains negative denominators and clamps their magnitude away from zero. Normalize is safe at zero. Lerp permits extrapolation. Clamp orders its bounds. Power clamps its base to a small positive value. Noise strength zero produces zero.
- Shader editor parameter controls, unlit control, new node templates, live validation messages, save blocking for invalid graphs, and Undo/Redo (Ctrl+Z/Ctrl+Y, up to 64 edits). Text editing retains native text undo. Node preview math was updated; previews use illustrative geometry and fallback textures, not the actual scene material.
- Float serialization uses round-trip precision for shader defaults. Invalid saves leave the working asset and loaded materials intact.

## Try it

`Shaders/PulsingRim.plutoshadergraph` and `Materials/PulsingRim.plutomaterial` are included in roads and RocketLeg. Assign the material to a smooth mesh. The graph combines world normal/view direction with a time pulse; override Tint, RimColor, RimPower and PulseSpeed in the material editor. The earlier Outline assets still work.

## Boundaries and remaining features

This is a bounded surface-graph implementation: at most 64 evaluated operations (including constants and material inputs), 256 editor nodes and 1024 links. Invalid/oversized programs report an error instead of indexing outside GPU arrays. Default standard materials retain the direct surface path.

General vertex displacement, custom render passes, tessellation, user GLSL/HLSL snippets, reusable subgraphs, arbitrary additional texture/sampler parameters, scene-depth/scene-color sampling, and a live 3D material preview are not implemented. Graphs do not yet run in shadow, voxel-GI, bake or legacy transparency shaders; procedural opacity may therefore differ in those passes. The RHI surface evaluator does run for alpha-mask and transparent colour draws. The earlier outline pass remains limited to opaque standard meshes. Time is not a gameplay clock.

## Validation

The GPU regression checks exercise parameter-driven colour, default/override behavior, instancing, alpha-mask and transparent opacity, signed division and material texture sampling on OpenGL and Vulkan. Compiler checks reject malformed and oversized graphs. Disk round-trip and live material refresh checks cover graph parameters/unlit/outline settings and failed-save retention. Existing outline, batching and temporal-motion regressions are also run.
