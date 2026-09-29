# Change-driven scene and renderer updates

## Investigation result

The engine already caches rigid mesh uploads, entity world transforms, mesh
commands, prepared draw packets, and opaque batches. The remaining opportunity is
to make invalidation authoritative from the scene through GPU residency, rather
than repeatedly walking the scene to discover that nothing changed.

The implementation now connects resource revisions, scene change queues,
immutable GPU records, and incremental packet preparation. Compatibility paths
remain for public mutable material references and procedural/animated producers;
those paths continue to validate values rather than assuming immutability.

## Evidence

The uncapped capture, frames 11299–11538, predates the latest renderer changes.
Across 240 frames, mean mesh submission is 0.184 ms, command translation 0.353 ms,
descriptor preparation 0.452 ms, and active RHI scene CPU time 2.911 ms. These
figures are nested/related scopes, not independent savings to add together.
The first captured frame reports zero mesh upload attempts, zero skinning updates,
and 261 reused packets / zero rebuilt. Mean scene GPU time is 11.668 ms and opaque
shading 8.293 ms. Change-driven updates primarily target CPU preparation and data
movement; they do not eliminate shading of unchanged visible objects.

## Existing behavior and gaps

| Layer | Existing behavior | Remaining repeated work / constraint |
| --- | --- | --- |
| Entity transforms | Dirty flags and revisions; unchanged setters return early | Ancestor changes must invalidate descendants |
| Mesh components | Cache commands by transform revision and mesh offsets | Scene visits every enabled component and republishes every frame |
| Retained scene | Reuses producer commands and stable handles | Frame leases retire unpublished producers; synchronization scans producers and material dependencies |
| RHI meshes | Upload rigid geometry on cache miss; skin only changed poses | No mesh content revision in rigid cache; skinned source changes are detected by size/pose, not content |
| Materials | Prepare each shared material once per frame | Reconstruct surface, resolve textures, compare fields each frame; mutable `GetConfig()` bypasses setters |
| Draw packets | Retain rigid packets across frames and passes | Traverse lists to validate entries; mutable instance/pose arrays use conservative paths |
| Geometry parameters | Deduplicate some objects/materials within a frame | Upload again across frames; material block mixes surface, graph time, viewport and fog data |
| Vulkan uniforms | CPU shadow copies and frame-local upload arena | Arena epoch changes require a fresh copy even without `UpdateBuffer` |

Relevant implementation locations:

- `engine/scene/src/Entity.cpp`: `MarkTransformDirtyRecursive`, transform setters.
- `engine/scene/src/Scene.cpp`: `SubmitRenderCommands`.
- `engine/scene/src/components/MeshComponent.cpp`: `SubmitRenderCommands`.
- `engine/render/src/RetainedRenderScene.cpp`: `Publish`, `Synchronize`.
- `engine/render/src/RhiSceneRenderer.cpp`: `prepareMaterial`, `collectSkinning`, `appendDraws`.
- `engine/render/src/BasicRenderer.cpp`: geometry `recordDraw`.
- `engine/render/src/rhi/vulkan/VulkanDevice.cpp`: `EnsureUniformResident`, `UpdateBuffer`.
- `engine/render/include/PlutoGE/render/Mesh.h`: `UpdateVertexData`.

`UpdateVertexData` changes mesh CPU data and the legacy GL VBO, while RHI rigid
uploads are keyed by mesh identity. Cloth calls this API. This is a potential
stale-geometry problem in the RHI path, established by code inspection rather
than a reproduced cloth rendering test. Broader caching must resolve it first.

## Recommended implementation order

1. **Authoritative resource revisions.** Add distinct geometry-content/topology
   revisions to meshes and content/residency revisions to textures. Propagate mesh
   edits into bounds, derived tessellation, rigid uploads, skinning caches and
   shadow invalidation. Replace unrestricted material mutation with setters or a
   scoped edit/commit API; migrate callers before trusting material revisions.
   Separate material surface changes from graph/pass topology changes. Use
   generation-bearing resource identities to handle destruction/address reuse.

2. **Persistent scene registration with queued changes.** Extend the retained
   scene with explicit register/update/remove operations. Components publish only
   on render-relevant changes; activation, visibility, destruction, scene moves,
   reparenting and asset replacement issue the appropriate operation. Retain the
   existing frame-lease path for transient producers during migration. Simply
   omitting existing `Publish` calls would remove objects. Maintain reverse
   material/mesh dependency lists so a shared resource edit queues its users.
   Move foliage and terrain to this model where their view-dependent generation
   permits it. Deduplicate queued changes; keep per-view visibility/LOD separate
   from persistent scene membership.

3. **Persistent GPU object and material records.** Split surface constants from
   frame/view constants, graph time, and transparent-pass fog. Allocate stable
   records for object/material identities and update only changed ranges. Use
   backend-appropriate storage buffers or persistent uniform pages, not the
   transient Vulkan arena. Version each frame-in-flight copy and update it only
   after its fence; retire removed slots safely. Keep transient allocations for
   truly transient draws. Avoid one driver allocation per scene object.

4. **Consume change sets in draw preparation.** Update only affected packet,
   batch and spatial-index entries. Maintain reverse membership for efficient
   removals. Add pose and instance-data revisions owned by their producers,
   instead of trusting shared-pointer identity. This eliminates repeated full
   comparisons without assuming that `isStatic` means immutable.

Motion requires an explicit settling update: on the first stationary frame after
movement, previous transform becomes current transform. Camera cuts, viewport
history resets, newly visible objects and multiple views need correct history
epochs. Time-driven shader graphs and active simulation still advance even when
an object's transform is unchanged. Culling, LOD, transparency order and shadow
receiver demand may change with the camera alone; ordinary visible geometry is
still drawn every frame.

## Validation and measurement

Add counters for scene objects visited/queued/updated, resource revision misses,
packet entries checked/rebuilt, and persistent versus transient upload bytes.
Benchmark stationary scenes at increasing object counts, then change one object,
one parent, and one material shared by many objects. After frame-in-flight warmup,
a stationary scene should perform no object/material content uploads; a single
edit should touch only its dependents (plus required history settling).

Regression coverage must include parent movement, reparenting, hide/show,
component disable, destruction/address reuse, asset reload, in-place mesh edits,
cloth/skinning, shared material edits, graph/pass topology changes, instance
mutation, animation stopping, camera cuts, and two independent views. Verify both
Vulkan and OpenGL images and motion outputs across more frames than the number
of in-flight buffers.

Start with resource revisions and explicit producer lifetime, then persistent GPU
records. A CPU-only skip of uniform update calls cannot achieve the full upload
goal. Measure against a fresh capture of the rebuilt renderer; the older capture
does not establish the current bottleneck or justify a numerical speedup claim.


## Implemented design

- Materials have stable identities, revisions, no-op-aware setters, `ReadConfig`,
  and tracked `SetConfig` replacement. Shader asset reloads use tracked replacement.
  An escaped legacy mutable `GetConfig()` permanently opts that material into
  conservative validation; it cannot silently stale a revision cache. A material
  change epoch gates retained-scene dependency checks for fully tracked scenes.
- Mesh content edits invalidate only affected RHI packets/geometry and skinned
  sources, including same-size edits. Tessellated derivatives are discarded and
  retained commands rebuild their geometry dependencies. A global mesh epoch
  gates a compatibility scan; within that scan only changed mesh revisions queue
  affected components. This is not yet a per-resource subscriber list.
- Texture identities, content revisions and weak lifetimes protect cached uploads
  and asynchronous normal-map completion. Material reuse also checks texture
  residency changes and retries unresolved texture inputs.
- Scene scopes replace per-mesh frame leases for rigid mesh components. Transform,
  hierarchy, activation, component enable/visibility and authored mesh changes
  queue updates. Scene destruction/switching retires registrations; generation
  checks protect reused scope addresses. Explicit removals cannot delete a producer
  that has already migrated into another scene scope.
- Moving rigid objects publish one settling update to clear motion. Returning
  scenes and reactivated objects reset history. Animated meshes remain on the
  transient submission path; camera visibility, LOD and sorting remain per-view.
- Geometry object, material and instance constants share immutable records, with
  full equality checks after hash lookup. Moving objects/instances and time-driven
  graph material constants retain transient storage. Time-independent graphs no
  longer carry unnecessary changing time values. Dynamic instances still validate
  array contents; pointer identity alone never authorizes reuse.
- Vulkan retains immutable uniform offsets across its three fenced frame copies.
  Each copy is populated on first use, then survives arena epochs. Removed ranges
  are reused only after submission retirement. Each arena preserves its original
  32 MiB transient capacity and adds a 16 MiB persistent tail (48 MiB total extra
  across three copies). Tail exhaustion safely uses transient residency. Cache
  eviction uses age and an 8 MiB soft budget per renderer; active working sets may
  exceed the soft budget. OpenGL immutable buffers retain normal buffer storage.
- Stable packet list layouts patch changed payloads in place. Membership changes
  retain reconciliation/compaction. Existing batch islands rebuild only when their
  packet revisions change. Mesh invalidation no longer resets unrelated packets.
- The profiler reports created/reused geometry parameter records. Vulkan tracks
  persistent upload bytes independently, allowing warm-frame residency assertions.

These changes do not skip drawing stationary visible objects or time-dependent
simulation. Terrain/foliage and mutable procedural producers retain their existing
submission paths. Legacy materials with escaped configuration references cannot
use the full revision fast path until their writers migrate to tracked edits.

## Implementation validation

The editor and both RHI test executables build in `RelWithDebInfo`.
Twenty-two focused checks passed: two CPU/lifecycle tests, seven OpenGL tests,
and thirteen distinct Vulkan tests. Coverage includes general rendering,
transparency, VSM membership/residency, temporal motion, skinning, FSR2,
shader graphs (Vulkan), outlines, sky quadrature, occlusion, diagnostics,
parameter reuse and mesh replacement. The known legacy OpenGL shader-graph
sampler-limit failure from the previous investigation was not part of this run.

The stationary Vulkan regression reports **2,544 persistent parameter bytes on
cold first use, zero on warmed frames**, and no new geometry parameter records
on those warmed frames. A material edit must create a new record and change
pixels. This counter excludes the intentionally transient camera/frame data.
The initial residency failure exposed an accumulating profiling counter; its
per-frame reset was fixed, and the original zero-upload assertion now passes.

The CPU mechanism benchmark with 2,048 stationary producers measured 0.06449 ms
per frame with individual leases versus 0.000039 ms with one scene scope over
240 iterations. It excludes rendering, visibility, and component traversal and
is evidence of eliminating producer heartbeats, not a whole-frame speedup claim.
A fresh real-scene capture remains necessary.

Added regressions cover scoped registration without
per-object publications, explicit removal, scene lifetime/switches/migration,
material revision semantics, stationary GPU residency, changed material pixels,
same-size mesh edits, ancestor motion and settling, activation, visibility,
component enable, reparenting and removal. Existing temporal, shader graph,
skinning, batching, shadow and cross-backend image suites remain required.


Final logs:

- `out/build/change-driven-final-cpu-tests.log`
- `out/build/change-driven-opengl-tests.log`
- `out/build/change-driven-vulkan-tests.log` (six passing scenarios plus the
  intermediate counter failure, superseded by the next run)
- `out/build/change-driven-final-vulkan-tests.log`
- `out/build/change-driven-final-vulkan-detailed.log`
- `out/build/change-driven-final-build.log`
- `out/build/change-driven-verification-build.log`

The new profiler lines are `RHI persistent uniform upload` and
`RHI persistent geometry parameters`. Compare them after at least three scene
submissions, together with retained producer publications/rebuilds, mesh uploads,
packet rebuilds, and total scene CPU time. Mesh asset rebuilds initialize vertex
history from the new geometry; skeletal deformation retains its existing
previous-vertex history path. Ordinary object motion retains and then settles
previous transforms, including instanced geometry and temporal resets.
