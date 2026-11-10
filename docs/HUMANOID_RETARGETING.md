# Automatic humanoid reference-pose alignment

## Problem and plan

Humanoid mapping identifies corresponding bones, but mapping alone does not make their
reference poses equivalent. Applying a source bind-relative rotation to a target bind
rotation preserves any A-pose/T-pose difference. An idle can therefore leave the target's
arms raised even with correct mappings.

The solution separates three concerns: character coordinate conversion, anatomical
reference-segment alignment, and animated rotation deltas. It runs in the shared runtime
retargeter, so clip playback, graph blends, preview poses and controller-timed traversal
use the same correction. It does not rewrite imported models or animation assets.

## Implementation

`ResolveReferencePoseCorrections` builds a correction per mapped target joint in the
existing per-clip retarget cache. It compares the bind-space direction to the next
corresponding anatomical landmark on each rig, converting the source direction into the
target character frame first. A shortest-arc quaternion aligns the target reference
segment to the source reference segment. This transfers posture without copying DCC
Euler angles, target bone lengths, or source translation into the target limbs.

The global rotation becomes:

```
animatedSourceDelta * referenceSwing * targetBindRotation * manualRotationOffset
```

The existing hierarchy solver converts desired global rotations back into target local
rotations. Each mapped segment is solved independently, so a child's animation does not
apply its parent's correction twice. Wrist, head and toe orientation inherit the terminal
chain correction. Antiparallel segments use a stable perpendicular axis. Hips translation,
root-motion stripping, scale and explicit rotation offsets retain their existing roles.

Corrections cover shoulders, arms, legs, feet, neck and available spine landmarks. Both
rigs must supply the same endpoint and a valid descendant relationship. Missing source
bind data, incomplete mappings, nonfinite directions and zero-length segments keep the
identity correction. That preserves the older behavior for unsupported chains rather
than inventing a pose. Axial twist remains based on the target bind orientation and
source animated rotation delta; a single segment cannot establish an absolute twist.

The cache signature includes target local bind matrices, so edited reference poses
invalidate the correction. Replacing clips already invalidates the clip caches.

### Fingers and other unmapped joints

Fingers and twist bones have no humanoid slot; their channels bind by joint name. Once a
mesh has a humanoid map, a name-bound joint below a mapped one is retargeted like a limb
(`ResolveUnmappedBodyCorrections`): it receives the source's global rotation delta, and
where both rigs have a name-matched child, the segment is aligned to the source's
reference direction. Joints without such a child (finger tips, twist bones) inherit their
parent's correction. These joints keep their own bind translation, so source bone lengths
never reach the target.

Copying the source's local transform instead mangles fingers between unrelated rigs. An
Unreal mannequin hangs each finger off a metacarpal and points bones along local +X; an
MPFB `game_engine` rig parents fingers to the hand and points them along local +Y. Thumbs
differ most: their rest direction can lie close to the source's curl axis, so a rotation
delta alone barely moves them. On SurvivalGame's Character6 with Unreal clips, the worst
finger segment error dropped from 95 to under 5 degrees, the same residual as the arms.

Joints with no mapped ancestor (Unreal `ik_*` bones under `root`, for example) still copy
the source's local transform by name. Mixamo-named fingers do not bind to Unreal-named
fingers, so those fingers keep their bind pose.

## Usage

Configure and save the target mesh's Humanoid Rig mapping, then play the existing
animation graph. No additional button or per-character pose offsets are required for
reference alignment. Old clips without source bind transforms need reimporting through
the current importer. Manual Rotation Offset remains an artistic override.

Reference-pose retargeting does not rename bones. Skeletal pose queries and IK share
`FindSkeletonJoint`: exact names take precedence, core humanoid aliases resolve through
the saved target mappings, and common Blender/Unreal/Mixamo finger aliases match only
when unambiguous. Existing soldier hand/foot queries therefore also work with the mapped
Mixamo ninja. Unrecognized custom finger names still require exact joint names.

## Rigs that do not look like the source

Three things decide whether a map works, and none of them shows up as a missing bone. The
Humanoid Rig section lists hierarchy problems in orange above the bone table.

**Slots follow the hierarchy, not the numbering.** Spine, Chest and UpperChest must run from
the pelvis upward. MakeHuman/MPFB numbers `spine05` at the bottom and `spine01` at the top,
the reverse of Unreal, so `Auto Map Missing` now orders the chosen joints by depth. A source
rig with more spine bones than the target needs Source Overrides so the top of the source
spine reaches the top of the target. For Unreal clips on an MPFB rig:

| Slot | Target joint | Source Override |
| --- | --- | --- |
| Spine | `spine04` | |
| Chest | `spine02` | `spine_03` |
| UpperChest | `spine01` | `spine_05` |

**A slot takes one source bone.** A target joint can share a name with a foreign source bone:
MPFB's pelvis joint is `root`, which is also the bone that carries travel in Unreal clips.
When several source bones claim one slot, a Source Override match beats an alias, an alias
beats a shared joint name, and the losers are not retargeted. Clips authored on the rig
itself have no competing pelvis, so their `root` still drives the hips.

**Pelvis joints per side.** `pelvis.L`/`pelvis.R` (or `Hip.L`/`Hip.R`) take no slot. Three layouts
occur, and `Auto Map` handles each:

- *Rigify* (Blender): `spine` is the real pelvis, with the side bones as children. Generic names
  would put it in Spine, so `AutoMapRigifyTorso` reads Rigify's numbering (`spine.001`-`.003`
  torso, `spine.004` neck, `spine.006` head) first. `forearm`, `shin` and `toe` are aliases too.
- *Pelvis named like a spine* elsewhere: the joint carrying both legs moves from Spine to Hips and
  the spine slots move down.
- *Split pelvis*: the torso and each leg hang from separate skeleton roots. Map Hips to the root
  above the spine and leave the leg roots unmapped: they then move rigidly with the hips
  (`FindHumanoidHipsFollowers`, shared by the runtime and the editor). Mapping Hips to one side
  is reported as one problem naming the joint to use. Spine and the chest slots may stay
  unassigned on a rig with one torso bone, though that bone follows the pelvis and does not
  receive the source spine's bending.

## Mesh editor

The Mesh Editor's Humanoid Rig page lists the slots by body part beside a diagram of the mesh's
bind pose (front or side, over its silhouette). Mapped joints are labelled with their slot in the
body part's colour; split-pelvis roots show as diamonds labelled "Follows Hips". Select a slot,
then click its joint: the next empty slot is selected, and a joint already used elsewhere moves
rather than being shared. Joints at the same spot ask which one was meant. `Mirror Sides` fills
empty slots from the other side by swapping the side in joint names. Mapping logic lives in
`HumanoidRigAnalysis.h` (engine) and `ui/meshedit/HumanoidRigEditing.h`; the diagram is
`ui/meshedit/HumanoidRigDiagram.h`. `PlutoGEMeshEditorTests [capture prefix]` covers both and
writes page screenshots when given a prefix.

## Validation

`PlutoGEHumanoidRetargetBasisTests` exercises A/T pose combinations, both arms, differing
bone roll, multiple animated arm angles, limb-length preservation, antiparallel reference
segments, missing bind metadata, zero-length segments, live bind edits, manual offsets,
Y-up/Z-up conversion, root-motion stripping and graph offsets. Optional arguments
`<mesh path> <native clip path>` validate real assets without writing to the project.

Read-only idle checks measured a maximum arm direction error of about 1.05 degrees on
the imported ninja and 4.68 degrees on the original soldier. The metric compares the
actual target upper/lower arm segment directions against the source animation in the
common character frame; it does not measure skin deformation or finger articulation.

For the soldier traversal baseline after changing the project prefab, set
`TRAVERSAL_TEST_MESH=project://Characters/Soldier/Model/Soldier.plutomesh` when running
`Tests/run_traversal_tests.py`. This overrides only the temporary test fixture, preserving
the user's current prefab and scene.

With a real mesh and clip, the same run also reports a finger pose error: the direction of
each finger's 01-02 and 02-03 segments and the bend between them, for Unreal-named fingers
present in both rigs. It fails above 5 degrees.

The ninja also passed read-only walk/run pose checks (maximum arm direction error below
1.2 degrees at clip start). The soldier traversal baseline passed all 60 scenarios at
60/240 Hz. Results are in the sample project under `Tests/retarget-native-results.txt`
and `Tests/retarget-pose-results.txt`.
