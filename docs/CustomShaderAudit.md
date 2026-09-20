# Custom shader audit and implementation

The previous graph only generated legacy geometry GLSL. The OpenGL/Vulkan RHI scene translator copied fixed material properties and ignored the graph. Graph variables were serialized but could not be referenced by nodes, were not bound, and were lost when the material editor saved. Invalid links/cycles could fall back silently; scalar/vector expressions were emitted without checking dimensions. Unlit behavior was tied to one builtin reference.

## Implemented

- A validated, topologically ordered surface program shared by legacy GLSL and OpenGL/Vulkan RHI. Existing nodes now execute in the RHI renderer, including instanced draws and transparent surface draws. Output controls albedo, world-space normal, metallic, roughness, opacity and emission.
- Named Float, Vec2, Vec3 and Color parameters with defaults, Parameter nodes and explicit per-material overrides. Save preserves overrides; graph saves refresh loaded materials. Switching shaders retains unmatched overrides without applying them to unrelated parameters. A changed parameter type requires resetting that override.
- Unlit mode on any graph asset.
- Vec2, Vec3 and Color nodes expose both packed and scalar outputs. Enable Component Pins to combine scalar inputs; disable it to split a packed input. Existing links keep their pin names.
- Time, World Position, World Normal, View Direction, Dot, Sine, Power and One Minus nodes. Scene updates drive Time using simulation delta, including runtime time scale; standalone rendering falls back to a monotonic clock. Motion vectors evaluate displacement at the previous frame's time.
- Material Texture Sample nodes sample the material's albedo, normal, metallic or roughness texture at a supplied Vec2 UV. Color and individual RGBA outputs are available. Raw normal samples still need remapping from 0..1 to -1..1 and conversion to world space before connecting to Normal.
- Deterministic compilation and parameter hashing; batching separates different programs and overrides. Parameter changes keep register layout stable.
- Validation of IDs, pins, duplicate connections, cycles, output count, parameter references/types, finite values and connected vector dimensions. Scalars broadcast; different vector widths require an explicit conversion using the existing vector/component nodes. Division retains negative denominators and clamps their magnitude away from zero. Normalize is safe at zero. Lerp permits extrapolation. Clamp orders its bounds. Power clamps its base to a small positive value. Noise strength zero produces zero.
- Shader editor parameter controls, unlit control, new node templates, live validation messages, save blocking for invalid graphs, and Undo/Redo (Ctrl+Z/Ctrl+Y, up to 64 edits). Text editing retains native text undo. Node preview math was updated; previews use illustrative geometry and fallback textures, not the actual scene material.
- Float serialization uses round-trip precision for shader defaults. Invalid saves leave the working asset and loaded materials intact.

## Try it

`Shaders/PulsingRim.plutoshadergraph` and `Materials/PulsingRim.plutomaterial` are included in roads and RocketLeg. Assign the material to a smooth mesh. The graph combines world normal/view direction with a time pulse; override Tint, RimColor, RimPower and PulseSpeed in the material editor. The earlier Outline assets still work.

## Expanded graph features

- **Custom direct lighting** exposes Light Direction, Light Color, Light Attenuation and Shadow Attenuation to an optional Direct Lighting output in the OpenGL/Vulkan RHI renderer. Step, Floor and Smoothstep nodes and expressions support graph-built ramps. See [ToonShaders.md](ToonShaders.md) for the reusable example, stage rules and renderer limitations.

- **Vertex Offset** accepts a world-space Vec3 displacement. The same vertex program runs in geometry, transparency, conventional shadows, voxelization and CPU baking. Instanced geometry is supported. Original mesh bounds cannot predict arbitrary displacement, so affected objects bypass bounds rejection. Displacement does not automatically rebuild normals; connect a world-space Normal when the effect needs it.
- **Tessellation** selects zero to three levels of cached, uniform CPU triangle subdivision before vertex evaluation. Each level produces four times as many triangles, retaining UV seams, submesh ranges and interpolated skin weights. Subdivision stops before exceeding one million triangles. This is not adaptive hardware tessellation.
- **Reusable Subgraph** references another shader asset. Inputs A–D correspond to its first four named variables; disconnected inputs use defaults. Outputs expose the child graph's material outputs and Vertex Offset. Subgraphs are flattened and validated, including recursive dependency checks.
- **Code Expression** accepts portable GLSL/HLSL-style math using inputs A–D, arithmetic, `sin`, `normalize`, `dot`, `pow`, `mix`/`lerp`, `clamp`, `saturate`, and `vec2/3/4` or `float2/3/4` constructors. An optional `return` and trailing semicolon are accepted. Expressions lower to ordinary graph operations, so all rendering and bake paths share their behavior. Arbitrary statements, loops, functions, includes and raw shader programs are not accepted.
- **Texture parameters** provide up to four additional named textures across the flattened graph, with linear/nearest filtering and repeat/clamp addressing. Material assets can override their references. Texture Sample selects these parameters or the four standard material textures. Additional textures are sampled as linear data.
- **Screen UV**, **Scene Color** and **Scene Depth** read the opaque scene snapshot in the transparent material path. Materials using scene reads are routed to alpha blending. Scene depth is the renderer's raw depth value, not linear eye distance. Scene reads cannot drive vertex displacement and do not include previously drawn transparent overlays.
- **Additional material passes** reference up to four graph assets, rendered as transparent overlays on the same mesh after the base material. Nested dependencies are checked and total expansion is capped at 32 passes. Equal-depth overlays retain their declared order. Each pass can use its own displacement, tessellation and textures. These are material overlays, not arbitrary custom render targets or render-graph scheduling.
- **Live material preview** renders the unsaved graph on a sphere in the graph editor, including material passes and additional textures. The material editor preview resolves graph parameters and texture overrides too. The small per-node diagrams remain illustrative.

Graph surface evaluation now participates in conventional shadow alpha testing, voxel GI, CPU baking and legacy transparency. Graph casters select conventional shadows instead of the virtual-shadow path. Voxel rebuilds capture one animation time for all chunks. Graph-dependent bakes use the CPU path and snapshot time; view-dependent expressions use a normal-incidence view convention. Bake albedo, opacity, normal, emission and displacement come from the graph. Raw normal texture sampling still requires explicit world-space conversion.

### Example assets

The following shader/material pairs are supplied in RocketLeg and the roads project:

- `DynamicWaves`: animated displacement, expression code and subdivision; try it on a plane.
- `ScrollingTexture`: animated UVs and a named `Pattern` texture override; assign a texture in the material editor.
- `SceneTint`: scene-color sampling through a tinted transparent material.
- `LayeredRim`: an additional `RimOverlay` pass that reuses the `PulsingRim` subgraph.

## Limits

This is a bounded surface-graph implementation: at most 64 evaluated operations (including constants and material inputs), 256 editor nodes and 1024 links. Invalid/oversized programs report an error instead of indexing outside GPU arrays. Default standard materials retain the direct surface path.

Dependency nesting is limited to eight levels. Expressions and subgraphs share the 64-operation budget. The implementations above cover material-level use cases; native adaptive GPU tessellation, unrestricted GLSL/HLSL programs, an unbounded descriptor system, and arbitrary custom render-target passes remain outside this graph API. The earlier outline pass remains limited to opaque standard meshes.

## Validation

The GPU regression checks exercise parameter-driven colour, default/override behavior, instancing, alpha-mask and transparent opacity, signed division and material texture sampling on OpenGL and Vulkan. Compiler checks reject malformed and oversized graphs. Disk round-trip and live material refresh checks cover graph parameters/unlit/outline settings and failed-save retention. Existing outline, batching and temporal-motion regressions are also run.

The expansion adds checks for expression evaluation and errors, subgraph defaults and inputs, subdivision topology/UVs/cache reuse, named textures, vertex displacement (including instancing and shadows), scene color/depth, equal-depth pass order, recursive dependencies and example-asset loading. A voxel-field readback verifies graph-generated emission energy in both Slang and legacy GLSL.

Validated on 2026-09-14 with the Release MSVC build: editor build succeeded; 15 selected CTests passed (both backend shader-graph, outline, batching, motion and RHI suites; voxel world cache, removed-character, OpenGL secondary bounce, emission coverage and post-process initialization). Interactive editor appearance and full scene bake output were not manually verified.
