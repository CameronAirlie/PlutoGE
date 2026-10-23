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
ThirdPersonProject applies it in `PoseRateLod`, by camera distance and by whether the camera can see the
character (behind the view or occluded drops to a low hidden rate). Procedural IK also changes the final
palette, so below full detail the project solves its foot and held-item IK at the pose rate too; between
solves the palette is unchanged and the actor is not re-skinned.

## Spawning cost

Instantiating a character used to cost 300+ ms, nearly all of it in `AnimationComponent` deserialization:

* **Clip cache.** `AssetManager::LoadAnimationClipAsset` keeps decoded clips by resolved path, revalidated by
  file write time (packaged clips are kept). A graph with hundreds of clips no longer re-reads and decodes every
  `.plutoclip` for each instance.
* **Shared clip data.** `AnimationClip::channels` is a `CopyOnWriteVector`: the cache and every component
  playing a clip read one copy of its channels. Reads never copy; a writer calls `channels.Mutable()` (or
  `push_back`, ...), which takes a private copy first if the channels are shared, so editing a clip never
  changes the cache or another character. Before this, each instance deep-copied every decoded clip: 32 ms of a
  ~39 ms Mercenary spawn. `PlutoGEAnimationClipSharingTests` checks sharing, detaching and playback.
* **Lazy retarget caches.** Each clip's retarget binding cache is built the first time the clip is sampled,
  not for every clip on the first evaluation, so a character pays only for the clips it plays.
* **Prefab preloading.** `Prefab::Preload` (C#: `Prefab.Preload`) parses a prefab ahead of time.

A mercenary on the ThirdPersonProject's 268-clip graph went from about 330 ms to 60 ms per spawn, and to about
7 ms of instantiation (a ~19 ms frame including its scripts) once clips were shared. The remaining cost is
hierarchy allocation (~4 ms) and the first instantiation of each held-item prefab nobody preloaded.

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

## Submesh count

Skinned commands are never cached by the RHI draw preparation (poses change every frame), so each submesh of a
character costs command translation plus a draw in every pass it reaches (visible, coverage depth, VSM receiver
and caster pages). A character exported with one material per part multiplies that: the ThirdPersonProject's
CoD Soldier had 46 submeshes, about 57 draws per guard per pass with its weapon, and 13 guards cost ~3.5 ms of
render CPU while the GPU barely changed. Merge parts that share material settings:
`PlutoGEMergeMeshSubmeshes project-root mesh-reference --report` lists submeshes, materials and UV ranges;
with a plan file (`MATERIAL`, `SUBMESH`, `SOURCE` lines; see the tool's header) it writes a mesh whose
sources are concatenated per LOD, with each source's UVs moved into its rectangle of a caller-built texture
atlas. Skeleton, humanoid map and LODs are kept. The project's `Tools/make_soldier_atlas.py` builds the atlas
and plan (46 -> 4 submeshes; gate view 17.6 -> 15.5 ms).

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
