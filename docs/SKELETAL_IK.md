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

Build editor/runtime and ScriptCore together after bridge changes, then rebuild
CoD scripts. `PlutoGESkeletonAttachmentTests` checks reach limits, degenerate
poles, bone lengths, descendant propagation, invalid targets and bind-pose IK.
Final grip quality requires a rigged asset and visual runtime testing.
