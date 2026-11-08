# M11–M14 implementation plan

The milestone acceptance criteria in FEATURE_MILESTONES.md remain the completion
gates. Foundations and automated unit checks alone do not complete a milestone.

## M11: Sequencer

1. Add a deterministic timeline model with bounded, validated tracks and keys,
   step/linear/smooth interpolation, explicit event intervals and looping.
2. Bind tracks by scene entity ID to local transforms, camera parameters, light
   properties, sound emitters and script events. Resolve bindings on every apply;
   report missing entities/components and allow rebinding.
3. Persist authoring data through component serialization and prefab remapping.
   Add editor track/key editing, capture keys, preview/scrub and play/stop.
4. Preview must capture original values once and restore on stop, scene change,
   component deletion and play transition without polluting history.
5. Validate interpolation, duplicate keys, boundary events, multiple loops,
   malformed input, missing bindings, history and runtime lifecycle.

## M12: Additive scenes

1. Introduce section handles with generation counters and ownership separate from
   scene-local IDs. Keep the persistent gameplay world as the simulation owner.
2. Read section bytes asynchronously with cancellation and bounded storage; parse,
   instantiate and activate on the engine thread because deserialization currently
   creates device-backed assets. Publish only fully validated sections.
3. Remap all serialized entity references on activation. Unload through normal
   entity destruction, cleaning scripts, physics, navigation, audio and rendering.
4. Expose request/progress/failure/activation/unload to managed scripts; integrate
   shutdown, play/stop and replacement. Add distance-based streaming volumes.
5. Test simultaneous loads, cancellation at each stage, ID collisions, failed
   loads, cross-section references and repeated unload/reload on both backends.

## M13: Replication

1. Layer a versioned replication protocol above existing managed networking.
   Use session/network IDs distinct from engine IDs, server authority, ownership,
   monotonic revisions, bounded properties and spawn/despawn/snapshot messages.
2. Apply incoming messages on the game thread and resolve entities through a
   session-owned mapping. Reject invalid schemas, unauthorized updates, stale
   revisions and resource-limit violations before mutating the world.
3. Add transform snapshot interpolation, late-join snapshots, disconnect cleanup
   and section-unload handling. Supply a two-player cooperative sample.
4. Test the protocol deterministically plus reproducible server/two-client
   sessions, including malformed traffic and disconnect during scene transition.

## M14: Roads

1. Extend existing SplineComponent (which already supplies banked mesh generation,
   adaptive sampling, material references and collision) rather than duplicate it.
2. Separate section geometry/cache from GPU publication; invalidate the affected
   Catmull–Rom neighbourhood on edits. Generate deterministic LODs and placement.
3. Add terrain projection, guardrails, junction authoring and mesh export using
   existing assets/cooker. Preserve collision/material and prefab ownership.
4. Expose settings through serialized inspector properties and scene history.
5. Test deterministic geometry, seams/banking, degenerate inputs, local rebuild
   coverage, collision/LOD consistency, undo and cooked export.

## Validation and delivery

Build affected libraries, editor, runtime and managed SDK. Run focused data tests
first, then scene/history/prefab/cooking regressions and multi-client tests.
Record interactive OpenGL/Vulkan checks separately from automated checks. Update
each milestone status only when its stated acceptance criteria have been met.

## Current checkpoint

The implementation now covers the scoped feature criteria. Final milestone
acceptance still requires interactive backend verification.

- M11: all continuous channels have restoration tests; bindings support entity
  selection/drag-drop. Runtime events stop dispatching if a callback stops play.
- M12: packed and loose section loading, pruned cooking, startup/runtime script
  lifetime, light/physics cleanup, persistent navigation and agent invalidation.
- M13: negotiated transport sessions, two-player cooperative pressure-pad sample,
  real multi-client tests, and native ABI adapter lifetime/reentrancy tests.
- M14: cached local CPU segment rebuilding, per-segment LODs, junction mesh/collider
  bakes, roadside prefab bakes, terrain conformity through the existing editor
  action, and mesh asset export with LOD/material retention.

The guides record boundaries: synchronous scene activation/navigation baking,
combined GPU buffer replacement, explicit road/terrain/junction bakes, and explicit
logical network-section mapping. Interactive OpenGL/Vulkan authoring, audio and
cooperative playtests, plus Linux validation, remain pending and are not implied
by the automated tests.
