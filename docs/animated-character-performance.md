# Animated character performance

How the engine keeps skinned characters cheap, and the knobs a project uses.

## Pose evaluation (CPU)

`AnimationComponent` caches the **sampled base pose** (clips, blends, layers,
retargeting) separately from the **final palette** (base pose plus IK and
ragdoll). IK target, authored IK and ragdoll changes only re-run the cheap
post-pass. Only playback changes (update, seek, state or clip changes, mesh or
root-motion changes) resample. Re-sending an unchanged `SetTwoBoneIK` target
does nothing.

`AnimationComponent::PoseUpdateRate` (C#: `AnimationComponent.PoseUpdateRate`)
samples the pose at a fixed rate instead of every frame. Playback still consumes
real time, and IK and attachments still follow the held pose every frame.
Ragdolls ignore the rate. Changing the rate starts at a stable per-entity phase,
so a crowd entering the same rate does not resample on one frame. The
ThirdPersonProject applies it by camera distance in `PoseRateLod`.

## GPU skinning

* One compute dispatch per actor and pose, shared by every submesh and by the
  visible, shadow and GI lists. Every actor's palette is uploaded in **one
  transfer per frame**, as three affine `float4` rows per joint.
* Motion-vector history is read from the actor's previous output instead of
  blending a second, previous-frame palette.
* Source vertices are packed to 68 bytes: 16-bit normal and tangent frame,
  16-bit joint indices, and full-precision positions, UVs and weights.
  `RhiGpuSkinning::QuantizeSource` gives the exact values the kernel reads.
* **LOD-aware deformation.** Generated LODs are index ranges over one vertex
  stream. `BuildSkinningLodLayout` orders the GPU stream coarsest-LOD-first, so
  each actor deforms only the prefix its finest drawn LOD (visible or shadow)
  reads. A later list, or a view sharing the frame through
  `ReuseSkinningForFrame`, that needs a finer LOD deforms only the skipped tail.
  Those slots restart their motion history; already-deformed slots keep theirs.

## Skinned LOD generation

`MeshImporter` simplifies skinned submeshes with skin weights as attributes.
meshoptimizer allows 32 attributes, and normals and UVs use 5. Joints that never
influence the same vertex neighbourhood share a slot
(`SkinWeightAttributeSlots.h`), so dense humanoid rigs (50–60+ joints) now get
LODs. A rig that still exceeds the budget stays at LOD0.

Exports with split normals at almost every vertex lock the conservative
simplifier. When it cannot reduce a submesh at all, it retries in meshoptimizer's
permissive mode, protecting UV, lightmap-UV and skin-weight seams
(`SimplificationSeamLocks.h`). Review those LODs visually: permissive mode is
marked experimental upstream.

Native `.plutomesh` assets get LODs offline with `PlutoGEPrepareNativeMeshLods`.
Pass `project-root input-reference --report` to print an asset's per-level
triangles, LOD coverage and skinned-vertex prefix without writing anything.

## Shadow casters

`SelectShadowCasterLod` (in `RenderCommand.h`) is the single caster LOD policy
for the OpenGL shadow pass and the RHI and VSM path. A caster is never finer than
its visible LOD, honours the producer's floor and any backend floor, and is
clamped to the submesh's LOD count. `MeshComponent` exposes the floor as
**Min Shadow LOD** (`MinShadowLod`). A value of 1 on dense crowd characters cuts
shadow-page rendering without changing their visible geometry.

The RHI path previously ignored `minShadowLodIndex`. It now honours it, so
foliage's existing `MinShadowLod` (default 3) applies on Vulkan as well as
OpenGL.
