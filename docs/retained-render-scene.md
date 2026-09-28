# Retained render scene

## Scope

The 18219–18458 capture reports 1.22 ms submission, 1.88 ms visibility preparation,
1.35 ms combined visible/shadow/GI packet preparation and 0.41 ms opaque batching.
This implementation removes repeated rigid command submission/copying and gives
batch preparation a persistent owner. It does not predict a particular FPS gain.

`RetainedRenderScene` owns rigid producer records independently of any viewport
or graphics API. MeshComponent publishes a command set when its existing source
revision changes. Unchanged components renew one producer lease; their submesh
commands are neither copied nor re-expanded. Multiple publications before
synchronization coalesce into one update. Transform/history, geometry and material
binding revisions are tracked separately. Transform-only updates preserve record
addresses, list ordering and the previous view's LOD selection.

## Lifetime and mutation contract

- Producers have process-local identities; cloned components receive new ones.
  Scene registration handles contain a slot and generation. Retired generations
  never become valid when a slot is reused.
- `ClearRenderCommands` starts a submission epoch. All active producers renew
  their lease before the first view or compatibility snapshot is requested.
  Synchronization retires missing producers before reading material dependencies.
  Disabling, removal, changing scenes and switching to animated submission thus
  cannot leave an old rigid object visible. Publications and rendering use the
  existing single-threaded frontend contract.
- The producer revision covers the complete immutable command set, including
  previous-frame transforms and bounds. A source edit must advance it. Mutable
  pose/instance arrays are rejected by this API and retain transient submission.
- Materials still permit direct `GetConfig()` edits. The scene therefore checks
  material topology once per distinct material per submission epoch and tracks
  producer dependencies. Overlay or graph/tessellation changes rebuild affected
  producers. Ordinary shading edits continue through the existing RHI material
  revision validation. Geometry and material resources remain owned by the asset
  system; producer records do not extend their lifetime.
- Synchronization completes before borrowing views. Borrowed command references
  are valid for one synchronous rendering invocation, until another submission,
  visibility preparation, synchronization or clear. Consumers must not store them
  for asynchronous use. Generational producer handles are the durable identity.

## Views and compatibility

Editor RHI viewports and the runtime service use `RenderCommandView`, supporting
both contiguous procedural input and reference lists. Camera-visible rigid lists
store pointers to persistent records; only culled instance arrays need transient
command copies. GI selection also builds references instead of copying commands.
The existing RHI packet cache continues sharing prepared rigid data across passes.

Legacy OpenGL passes and callers of `GetSceneRenderCommands` /
`GetVisibleRenderCommands` still receive contiguous compatibility snapshots.
Materializing legacy commands is isolated from the modern view path. Existing
terrain, foliage, skinned and procedural submission APIs remain supported.

Visibility and LOD selection still scan the scene. Component enumeration also
remains, performing activity/revision checks and one lease per rigid producer.
This intentionally preserves inherited transforms, activation and material-edit
behavior before introducing a spatial index or fully event-driven ECS registry.
Prepared GPU draw lists remain contiguous at the existing backend boundary;
persistent GPU object tables and indirect main-pass submission are separate work.

## Persistent opaque batches

`RetainedOpaqueBatches` partitions the ordered visible packet list into contiguous
groups whose compatible draws cannot occur outside the group. It uses the last
occurrence of each mesh/surface key, so interleaved compatible materials stay in
the same group. Hash collisions only enlarge groups; the existing full equality
checks still decide instancing and range merging.

Each group retains its source preparation revisions and batched output. A changed
group reruns the existing batching algorithms; unrelated groups reuse their
instance buffers and preparation tokens. A zero revision always forces rebuilding.
Groups absent from the current list are removed, bounding cache growth. Output
order and the previous global algorithm's merge boundaries are preserved.

## Diagnostics and checks

Profiler exports/UI report active producers/commands, publications, unchanged
leases, removals and command writes. RHI exports report reused/rebuilt opaque
groups when the visible packet list changes; an unchanged list bypasses batching.

The new CPU target covers unchanged record reuse, coalesced writes, independent
revisions, motion history, overlay changes, removal, recycled generations, mutable
array rejection, mixed transient/retained views and incremental batching against
the original global algorithm. Both RHI image suites exercise borrowed frontend
views against contiguous reference rendering, material edits, movement and removal.

Validation passed in Release: editor/runtime builds, the retained-scene CPU target,
full Vulkan and OpenGL RHI suites, EngineGraphicsApiTests (including component
motion/animation and visibility checks), and VulkanVsmPerformanceTests. The CPU
test also submits 2,048 unchanged synthetic commands for 240 frames through each
API: the retained path performs zero command writes after warm-up. Its timing
excludes actual assets, visibility and GPU work and is not a scene-speed estimate.

For a matched scene capture, compare submission and visibility CPU, packet/batch
preparation, producer publications versus leases, and reused versus rebuilt batch
groups. A stationary rigid scene should report zero command writes after motion
history settles. Camera movement still performs visibility/LOD work and can alter
batch membership; it should not republish rigid producer payloads.
