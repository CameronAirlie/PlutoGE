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
// Optional bone-space offset, e.g. converting this rig's hand frame into an item grip frame.
bound = SkeletonAttachments.Bind(gunSocket, soldierMesh, "hand.R",
    Vector3.Zero, Quaternion.CreateFromYawPitchRoll(0, MathF.PI / 2, 0));
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
new bone immediately. The offset overload sets the attachment's PositionOffset
and RotationOffset (applied in bone space); the plain overload keeps existing
offsets. Bone names resolve like `TryGetBoneWorldPose`: exact joint names first,
then humanoid aliases. `Release` removes the attachment component and retains
the socket's current parent and local pose; it does not create weapon physics.
Bone pose reads and attachments use the same final skinning palette as rendering.
Do not target the hand's IK from a socket attached to that same hand; solve the
primary hand first, then derive the other hand's target from the weapon grip.

No animation, rig, skin weights, automatic aiming, or gameplay state is generated
by these APIs. Static weapons work as children; static soldiers must be rigged
and skinned. The inspected CoD `Solider.glb` and `m16_assault_rifle.glb` have no
skins or animation clips.

## Hiding joints from cameras

A skinned `MeshComponent` can keep chosen joints out of every camera while the
mesh still casts its whole shadow. A first-person body uses this to hide its own
head. Set the joints with `MeshComponent::SetCameraHiddenJoints` (C#
`MeshComponent.CameraHiddenJoints`; serialized as `Camera Hidden Joints`, a
comma-separated list written only when set). Names resolve like bone queries:
exact names first, then humanoid aliases such as `Head`. Unknown names are ignored.

Each listed joint and every joint below it is shrunk onto the listed joint's posed
position in a copy of the skinning palette (`scene/JointHiding.h`). Vertices only
partly weighted to a hidden joint, such as a neck blending into the head, close
part of the way. The pose itself is untouched, so bone reads, attachments and IK
still see the head. While joints are hidden, the mesh submits two draws per
submesh:

- The full pose, marked `RenderCommand::shadowOnly`. It goes into shadow maps and
  GI's scene list.
- The camera's draw with the joints collapsed, which casts no shadow.

`RhiSceneRenderer` drops shadow-only commands from the camera's list
(`CameraCommandSelection`) before any camera pass, for base, overlay and
render-texture cameras alike. Hiding joints costs one more skinned draw for that
mesh. An empty list draws the mesh as before.

## Clothing and outfits

Clothing, armour, hair or a backpack is a **wearable**: a prefab whose skinned
meshes are rigged to the character's armature (modelled over the body, exported
without it). A character wears wearables through an **Outfit**. The pieces are
independent, so each can be used on its own:

| Piece | What it does |
|---|---|
| `OutfitComponent` (Add Component > Character > Outfit) | The worn list. Spawns each wearable on the body, keeps it there, hides skin under it, reports fit. |
| `WearableComponent` (Character > Wearable, on a prefab's root) | The item's slot and the body joints whose skin it covers. Data only. |
| `MeshComponent` **Follow Parent Skeleton** | Poses a skinned mesh with its nearest skinned ancestor's skeleton (`scene/SkinPoseFollow.h`). |
| `MeshComponent::SetCoveredJoints` | Leaves body triangles under clothing out of every draw (`scene/SkinCoverage.h`). |
| `AssessSkinFit` (`scene/SkinPoseFollow.h`) | Whether a garment was rigged to this body's proportions. |
| Transient entities (`Entity::SetTransient`) | Generated content that is never saved ([Scene hierarchy](SCENE_HIERARCHY.md#generated-entities)). |

### Making a wearable

1. Import the garment's model (rigged to the character's armature).
2. Right-click it in the Content Browser and choose **Create Wearable**. This
   writes `<model>.plutoprefab` next to it. The prefab's root holds a Wearable,
   with a slot guessed from the name (`Boots` gives Feet, `Hood` gives Head,
   otherwise Body), and the mesh with Follow Parent Skeleton ticked
   (`scene::SaveWearablePrefab`). A model without a skeleton is refused.
3. Open the prefab. In the Wearable's inspector:
   - Choose the **Slot**, from the list or as a custom name.
   - Under **Hides body skin**, tick the body joints the garment covers. The
     checklist lists the garment's own skeleton, which is the body's armature, and
     can be searched.
   - Under **Fit check**, pick the character's body mesh. The inspector says
     whether the garment was rigged to that body (see Fit below).

### Wearing it

Add an **Outfit** to the character: on the entity with its skinned mesh, or any
entity above it. Add wearable prefabs to its **Worn** list (drag them in from the
Content Browser). Each row shows the item's state and fit (hover the icon), its
slot and the prefab. The items appear on the body in the editor too.

- **The body** is the skinned mesh on the Outfit's entity, or the first one below
  it. Meshes that follow another skeleton, generated entities and entities with an
  Outfit of their own are skipped.
- **Worn items** are spawned under the body at its origin as
  [transient entities](SCENE_HIERARCHY.md#generated-entities) tagged `Worn`. The
  list is saved; the items are rebuilt from it. Every mesh in a worn item follows
  the body's skeleton, whether or not the prefab ticked it. After an undo or a
  reload, an Outfit takes over the items it finds on the body instead of
  spawning them twice.
- **Slots:** one item shows per slot, the last one listed. Wearing a cloak over a
  coat (both Body) hides the coat, which shows again when the cloak comes off.
  The inspector marks the coat as covered. An item without a Wearable takes no
  slot.
- **Skin:** the body leaves out the triangles whose three corners are each
  mostly (half or more) weighted to a joint that a shown item covers. Each joint
  hides only the skin it carries, so a covered forearm keeps its hand. Nothing
  moves, so the edge of the hidden area stays where the body has it. The body
  draws a copy of its mesh without those triangles, keeping every LOD range,
  rebuilt only when the covered set changes. Shadows use the same copy.
- Disabling or removing the Outfit takes everything off and uncovers the skin.

From scripts, use the C# `OutfitComponent`: `Wearables` (get or set the list),
`Wear(prefab)`, `TakeOff(prefab)`, `TakeOffAll()`, `IsWearing(prefab)`. These go
through the generic registered-component bridge. Changes apply on the next frame.
Scripts on spawned wearables do not run.

### Fit

Each garment joint takes the pose of the body joint it matches, so a garment
rigged to a body of other proportions lands off it: its shoulders and hood sit
too high or too low, and skin shows through. Rest poses may differ (an A-posed
garment on a T-posed body is fine), so `AssessSkinFit` compares proportions: the
length of each bone between matched joints, and the height of the topmost matched
joint, in the two bind poses. It reports how joints matched (by name, by humanoid
mapping or by alias) and the largest difference:

| Quality | Largest difference | Meaning |
|---|---|---|
| Fits | up to 1 cm | Rigged to this body. |
| Loose | up to 3 cm | Wearable; may clip or float a little. |
| OtherBody | more | Rigged to another body. The Outfit still shows it, logs a warning, and marks the row. |
| Unmatched | no joint matched | It cannot follow this body at all. |

A garment that matches only by alias, never by name, was rigged to another
skeleton, and the report says so. The fix is always in the modelling tool: skin
the garment to this character's armature.

### Following a skeleton

**Follow Parent Skeleton** (`MeshComponent::SetFollowParentSkeleton`, serialized
as `Follow Parent Skeleton`, written only when set) works without an Outfit too.
For example, a mesh can be placed under a body by hand in the editor. The
follower has no animator. Each draw takes the skinning palette of the nearest
ancestor `MeshComponent` that has a skinned mesh and does not follow anything
itself, and remaps it onto the follower's own joints:

- Joints match by exact name, then by the humanoid bone the follower's own
  mapping gives a joint, then by humanoid aliases (`FindSkeletonJoint`). The
  follower can carry only the joints it is weighted to, in any order.
- Each matched joint takes the body joint's posed transform:
  `body palette x body bind x garment inverse bind`.
- A joint with no match, such as a coat-tail bone, moves rigidly with its nearest
  matched ancestor. One with no matched ancestor stays at its bind pose, and so
  does the whole follower without a skinned parent.

Whatever poses the body poses the garment too: the animator, IK, ragdolls and
editor preview. Joints hidden from cameras (above) resolve against the follower's
own skeleton by name, so a hood hides with the head in first person. Animators,
ragdolls and `SkeletonAttachmentComponent` skip followers when they look for the
character's mesh. Without the flag, a second skinned mesh under one animator
makes the animator re-evaluate its pose for whichever skeleton was drawn last. The
follower is drawn with its own entity transform, so it sits at its parent's
origin. Culling uses its bind bounds, like any skinned mesh.

`PlutoGERenderSceneShot --pose <seconds>` lets Outfits dress their bodies, plays
the animators, and draws skinned meshes through their own submission. Offscreen
review shots then show clothing on the posed body; without `--pose`, characters
stand in bind pose.

`PlutoGESkinPoseFollowTests` checks matching and the palette.
`PlutoGEOutfitTests` checks coverage, fit, transient entities, the wearable prefab
helper, and the Outfit (spawning, following, slots, coverage, respawning,
adoption after a rebuild, disable and removal). `PlutoGEOutfitInspectorTests`
draws both inspectors in every state.

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
requires a rigged asset and visual runtime testing. `PlutoGEJointHidingTests`
covers joint hiding: name resolution, descendants, partly weighted vertices,
the two draws a mesh submits, and serialization. The `--camera-stack` modes of
`PlutoGE{Vulkan,OpenGL}RhiTests` check that cameras never draw shadow-only
commands.
