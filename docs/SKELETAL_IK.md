# Skeletal IK and runtime attachments

`AnimationComponent.SetTwoBoneIK` installs a transient named constraint. It works
on a skinned bind pose without animation clips, and on both node-animated and
humanoid-retargeted skeletons. It runs after animation layers and before physics
ragdoll blending. Editor clip preview bypasses IK. Constraints are applied in
insertion order; updating a name preserves order. There is a maximum of 16.
They are runtime state, not serialized graph assets.

The root, middle and tip must be three directly connected skin joints, e.g.
upper arm, forearm and hand. Targets beyond reach are clamped without stretching.
A pole position controls elbow direction, with fallback for collinear poles.
Weight blends joint rotations; optional target rotation controls hand orientation.
Invalid chains or nonfinite inputs fail without changing the pose.
Nonuniform scale/shear across an arm chain is not an authoring target: apply rig
scale before export. Bone arrays need not be in parent-first order.

## Editor workflow

1. Select the soldier's Animation entity. Add Component > Two Bone IK Rig.
2. Assign **Mesh** to the skinned mesh entity (0 uses the Animation entity itself).
3. Choose RootBone, MiddleBone and TipBone from the imported bone names. Middle
   and tip choices are filtered to direct children; names are saved, not indices.
4. Click **Create Missing Targets**, or drag existing scene entities into Target
   and Hint. Generated targets are children of the animation owner, alongside
   the mesh, so they travel with the character and stay inside its prefab.
5. Click **Select Target** or **Select Hint**, then use ordinary viewport translate
   and rotate gizmos. Turn on viewport debug shapes for the cyan chain, yellow
   target marker and purple elbow hint. PreviewInEditor controls live posing.
6. Weight controls reach, RotationWeight controls hand orientation. Add another
   constraint for the other arm; constraints solve in saved list order. Disable
   a constraint or the component to release the pose. Removing a constraint
   keeps its target entities so other constraints/scripts can still use them.
7. Save the scene or prefab normally. Duplication remaps internal mesh, target and
   hint references. External targets remain external when cloning.

Editor clip inspection continues to bypass procedural IK. Stop clip preview to
inspect the authored IK pose. The rig resolves fresh entity transforms when a
pose is requested, including while paused; no script or frame-order dependency
is required. Constraints on bones belonging to their own target/hint attachment
hierarchy are rejected to avoid feedback loops. Script constraints solve after
editor-authored constraints, before physics ragdoll blending.

For a gun socket, add **Skeleton Attachment** to an empty entity below the skinned
mesh. Choose JointName and adjust PositionOffset/RotationOffset (degrees) for the
grip. Put weapon geometry beneath it. Bone names take precedence over legacy
node indices when an import changes bone order. Offsets default to zero, preserving
existing assets. The normal entity enabled checkbox controls attachment updates.

## Script APIs

```csharp
// animator belongs to the mesh entity or one of its ancestors.
bool posed = animator.SetTwoBoneIK("SupportHand", soldierMesh,
    "upper_arm.L", "forearm.L", "hand.L",
    foregrip.WorldPosition, elbowHint.WorldPosition,
    weight: 1, worldRotation: gripRotation, rotationWeight: 1);
animator.ClearTwoBoneIK("SupportHand");
animator.ClearAllTwoBoneIK();

bool bound = SkeletonAttachments.Bind(gunSocket, soldierMesh, "hand.R");
bool found = SkeletonAttachments.TryGetBoneWorldPose(soldierMesh, "hand.R",
    out var handPosition, out var handRotation);
SkeletonAttachments.Release(gunSocket);
```

Use actual imported bone names. The APIs return false for invalid entities,
missing bones, invalid chains or an unavailable bridge. IK world positions and
rotation are converted into mesh space, including the mesh offset, when called.
Call again each update when a target or character moves. Clear constraints on
respawn/reset and during actions that must release the hand. Ragdoll blending
takes precedence over IK at full physics weight.

`Bind` reparents an existing socket beneath the skinned mesh and replaces its
transform with the animated bone pose. Put the weapon beneath that socket; its
child transform supplies the grip position/rotation/scale. Rebinding selects a
new bone immediately. `Release` removes the attachment component and retains
the socket's current parent and local pose; it does not create weapon physics.
Bone pose reads and attachments use the same final skinning palette as rendering.
Do not target the hand's IK from a socket attached to that same hand; solve the
primary hand first, then derive the other hand's target from the weapon grip.

No animation, rig, skin weights, automatic aiming, or gameplay state is generated
by these APIs. Static weapons work as children; static soldiers must be rigged
and skinned. The inspected CoD `Solider.glb` and `m16_assault_rifle.glb` have no
skins or animation clips.

## Validation

Authored constraints optionally expose `WeightParameter`: an animator float
multiplies their saved weight, clamped to 0–1. An empty name preserves the saved
weight; a missing parameter resolves to zero. Use a float default of one for
support-hand IK and set it to zero while a reload clip controls the hand.

Build editor/runtime and ScriptCore together after bridge changes, then rebuild
CoD scripts. `PlutoGESkeletonAttachmentTests` checks reach limits, degenerate
poles, bone lengths, descendant propagation, invalid targets and bind-pose IK.
`PlutoGEIKComponentTests` additionally checks target movement/cache invalidation,
disable/removal, editor preview, malformed settings, scene round trips, prefab
reference remapping and bone-relative attachment offsets. Final grip quality
requires a rigged asset and visual runtime testing.
