# DungeonCrawler performance pass

## Scope and plan

The supplied Debug capture averaged 14.309 ms CPU frame time and 4.345 ms
scene GPU time. Prioritize unnecessary CPU dispatch and UI work, then GPU shadow
request contention. Preserve animation frequency, shadow coverage, visual
settings, gameplay behavior, and scene data.

1. Give passive components an explicit frame-update policy.
2. Benchmark the skinning kernel against an independent reference before
   retaining arithmetic or access-pattern changes.
3. Avoid replacing RmlUi subtrees for ordinary text updates.
4. Deduplicate VSM receiver requests locally without dropping coverage.
5. Build the Debug editor and run CPU, UI, Vulkan, and OpenGL regressions.

## Implementation

- `Component::RequiresFrameUpdate()` defaults to true. Mesh and collider data
  opt out; physics and render registries still consume them normally. A derived
  passive component that adds frame work must override the policy to true.
  The entity loop also avoids an intermediate copy of the trace context name.
- RmlUi plain-text updates reuse an existing text node. Unchanged text performs
  no mutation. Markup, escaped entities, empty content, and data-model elements
  retain the existing parser path. World-surface invalidation still occurs on
  changed content; subtree lookup/subscription invalidation is unnecessary when
  the node survives.
- VSM request workgroups use a 128-entry shared-memory cache. Atomic insertion
  elects one requester per cached key. Hash collisions use the original global
  atomics, so the cache cannot suppress a distinct page. Every lane initializes
  and synchronizes before out-of-viewport lanes exit. This preserves odd-sized
  viewports and page-guard footprints.
- The OpenGL shader normalizer translates Slang's workgroup/shared-memory
  acquire-release barrier to core GLSL `barrier()`, keeping other barrier
  scopes untouched. Vulkan keeps the original SPIR-V semantics.

## Measurements and decisions

The skinning translation unit already uses optimized compilation in Debug.
A proposed access-pattern change measured approximately 2.5 ms both before and
after for 73,683 four-influence vertices. It was discarded rather than retained
without evidence of improvement. A new CPU-only reference test and benchmark
remain, independent of graphics initialization.

Four VSM runs alternated original/optimized/optimized/original request shaders
with otherwise identical code. In the existing 582 x 507 moving-camera fixture,
receiver requests measured 0.087–0.170 ms originally and 0.013–0.014 ms with the
workgroup cache. The fixture reported identical cache hits and triangle counts.
Total planning timings overlapped across runs; these asynchronous microbenchmark
observations are not a measured DungeonCrawler frame-rate improvement.

## Validation

- Debug CPU skinning/reference/history test, GPU skinning rendering test,
  OpenGL and Vulkan VSM-only tests, scene/component-policy tests, and CPU trace
  tests passed.
- The existing GPU skinning fixture checked cascaded-shadow counters while
  inheriting the VSM default. It now explicitly selects cascaded shadows;
  its original assertions remain intact.
- Four paired VSM performance runs passed budget, cache, and rendering checks.
- DungeonCrawler's real `dungeon.rml` passed text-node retention, parser-fallback,
  layout, scrolling, and native drag/drop checks at three viewport sizes.

For end-to-end verification, capture the same gameplay route and enemy population
using the rebuilt Debug editor, with the same tracing and VSync settings. Compare
CPU frame time, component dispatch, HUD update/rendering, and VSM receiver-request
timings. Animation/skinning architecture and broader HUD layout costs remain
separate follow-up work; this pass does not move skinning to the GPU or lower
animation update rates.
