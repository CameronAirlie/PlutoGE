# RHI Post-Process Migration: Next-Agent Handoff

## Current state

- `PostProcessGraph` models named immutable resources and passes, derives dependencies, performs stable topological sorting, and rejects invalid graphs.
- Resource lifetimes are explicit: `External`, `Transient`, and `History`.
- Shared Slang fullscreen infrastructure emits GLSL for OpenGL RHI and SPIR-V for Vulkan.
- Tone mapping, gamma correction, FXAA, color grading, and chromatic aberration run through `BasicRenderer` on both RHI backends.
- `RhiPostProcessAdapter` is the typed boundary from editor-facing `IPostProcessEffect` objects to backend-neutral packets.
- Chromatic aberration is registered in the effect factory/dropdown and has a legacy OpenGL compatibility implementation.

## Required implementation order

### 1. Physical graph resources

Implement a `PostProcessResourcePool` owned by the RHI renderer, not by effects.

- Resolve scaled graph descriptors against the viewport with a minimum dimension of one.
- Allocate transient textures lazily and alias only when compiled lifetimes do not overlap.
- Retain history textures across frames; invalidate them on resize, camera cuts, backend changes, or incompatible descriptor changes.
- Import external textures without taking ownership.
- Keep allocation keys explicit: dimensions, format, usage, sample count, and history role.
- Add CPU tests for lifetime intervals and mock-device tests for allocation, reuse, resize, and destruction.

### 2. Graph execution and multiple inputs

- Add an executor that consumes `CompiledPostProcessGraph` and a pass callback/implementation registry.
- Bind sampled inputs by declared semantic rather than hard-coded effect-specific slots.
- Keep frame parameters separate from effect parameters.
- Add explicit output dimensions to rendering commands so half-resolution passes set viewport/scissor correctly.
- Validate that a texture is never sampled while attached for writing.

### 3. Scene/G-buffer exposure

- Extend the RHI geometry pass to produce depth, normals, albedo/material data, emission, and motion vectors.
- Extend `RenderingInfo` and both backends for multiple color attachments before adding G-buffer-dependent effects.
- Define one documented Slang binding convention for scene color, depth, normals, material data, motion, history, and pass-local textures.
- Preserve zero-to-one depth, reversed-Z, framebuffer-origin, and color-space conventions.

### 4. First multi-pass effects

Migrate in this order:

1. Bloom: prefilter, downsample pyramid, upsample, composite.
2. Lens flare: bright pass, flare generation, optional texture, composite.
3. Auto exposure: luminance reduction/history and adaptation.

Do not replace the bloom pyramid with a full-resolution blur. Match legacy parameter behavior and use graph-owned transient resources.

### 5. Depth/G-buffer effects

Migrate SSAO/LSAO, SSR, SSGI, volumetric fog, depth of field, and motion blur. Each adapter should use typed configuration snapshots rather than reparsing serialized strings. Share reconstruction, depth, noise, and bilateral-filter Slang helpers where practical.

### 6. Temporal and GI effects

- TAA owns double-buffered color/depth/normal history through graph history resources.
- Temporal SSAO/SSGI histories use the same invalidation service.
- LPV, RSM, and VCTGI require 3D texture support and should remain explicitly unsupported until the RHI supports the needed volume formats, usages, and passes.

### 7. Verification

- Keep graph and adapter tests GPU-independent.
- Add deterministic OpenGL/Vulkan image tests per migrated effect with non-default parameters.
- Compare images using tolerances, not exact bytes.
- Exercise resize and history invalidation.
- Run Vulkan validation and fail on validation messages or OpenGL debug errors.

## Architectural constraints

- Public RHI and graph headers must not expose OpenGL or Vulkan handles.
- Effects describe work and parameters; the graph/executor owns scheduling and GPU resources.
- Do not add backend branches to effect implementations.
- Do not silently run simplified substitutes for unsupported effects.
- Report unsupported effects explicitly until their resource requirements are implemented.
- Preserve the typed adapter registry as the serialization/editor boundary.
