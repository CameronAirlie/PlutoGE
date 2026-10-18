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

The ninja also passed read-only walk/run pose checks (maximum arm direction error below
1.2 degrees at clip start). The soldier traversal baseline passed all 60 scenarios at
60/240 Hz. Results are in the sample project under `Tests/retarget-native-results.txt`
and `Tests/retarget-pose-results.txt`.
