# Skeletal ragdolls

PlutoGE can simulate a skeletal mesh as an articulated Bullet ragdoll and can
optionally drive that ragdoll toward its animated pose. The result remains fully
physical: collisions and impulses can displace the character while joint motors
and a root controller try to recover the requested pose.

The system is split between two components:

- `AnimationComponent` owns ragdoll creation, destruction, impulses, and the
  blend between the animated and simulated skinning poses.
- `ActiveRagdollComponent` configures the physical controller that makes an
  enabled ragdoll follow the entity transform and current animation.

This separation allows the same ragdoll to switch between passive and active
behaviour without rebuilding the entity's component layout.

## Editor setup

1. Add a skeletal `Mesh Component` and an `Animation Component` to the entity.
   A mesh on a child entity is also discovered when it uses the owner's
   animation component.
2. Add **Animation > Active Ragdoll** from the Inspector's Add Component menu.
   The menu item is available after the entity has an Animation component.
3. Enable `Ragdoll Enabled` on the Animation component at runtime or from a
   script. Set `Ragdoll Weight` to `1` for the complete physics pose.
4. Play an animation. The active controller uses the unblended animation pose
   as its physical target, even while the rendered skeleton shows the ragdoll
   result.

Disabling only the Active Ragdoll component leaves ragdoll simulation enabled
but removes pose-following. This is the passive or "knocked out" state.
Disabling `Ragdoll Enabled` removes the ragdoll and returns pose ownership to
animation.

## Generated physics representation

Ragdolls are generated only while `RagdollEnabled` is true. Recognized humanoid
rigs create capsules for their mapped major bones; fingers, facial bones, twist
bones, IK helpers, and attachment joints remain animation-driven beneath their
nearest physical parent. Unknown rigs fall back to a complete representation so
they remain functional. Parent and child physical bodies are connected by
six-degree-of-freedom constraints with locked translation and bounded swing and
twist.

The implementation also provides:

- constrained angular friction for passive limbs;
- sleeping for passive bodies that have come to rest;
- continuous collision detection on the generated capsules;
- increased solver iterations and split-impulse penetration correction;
- filtered collisions for capsule pairs that overlap in the activation pose;
- shared collision with ordinary bodies in the scene's Bullet world; and
- automatic suppression of the owner's ordinary collider hierarchy while its
  ragdoll is active, avoiding two physical representations fighting each other.

The generated shape and mass values are currently automatic. Skeleton scale,
joint hierarchy, and bind-pose quality therefore have a direct effect on the
result.

Dormant Active Ragdoll components do not create bodies, constraints, collision
pairs, or per-joint target poses. Keep `RagdollEnabled` false on living
characters and set it once when transitioning into ragdoll simulation. Avoid
calling `ResetRagdoll()` every frame because each reset deliberately rebuilds
the articulated body.

## Active controller properties

| Property | Default | Effect |
| --- | ---: | --- |
| `PositionStrength` | `35` | Strength of the root body's pull toward the entity's animated world position. |
| `RotationStrength` | `18` | Strength of root orientation correction. |
| `Damping` | `8` | Opposes root linear and angular velocity to reduce overshoot. |
| `MaxForce` | `1200` | Upper bound on the root-following force. |
| `MaxTorque` | `80` | Upper bound on root correction and each joint servo. |

All values are clamped to zero or greater. `MaxForce` and `MaxTorque` are safety
limits as well as tuning controls: increasing strengths without suitable limits
can make impacts look rigid or cause oscillation.

The controller does not teleport the ragdoll. Moving the entity changes the
target pose, and bounded forces move the physical skeleton toward it. This
means the character can lag behind a rapidly moving target, be pushed away by
collisions, and recover dynamically.

## C# scripting

Components must be added to the entity in the editor or scene data before they
can be retrieved from a script.

```csharp
using System.Numerics;
using PlutoGE.ScriptCore;

AnimationComponent? animation = GameObject.GetComponent<AnimationComponent>();
ActiveRagdollComponent? controller = GameObject.GetComponent<ActiveRagdollComponent>();

if (animation is not null && controller is not null)
{
    animation.RagdollEnabled = true;
    animation.RagdollWeight = 1.0f;

    controller.Enabled = true;
    controller.PositionStrength = 35.0f;
    controller.RotationStrength = 18.0f;
    controller.Damping = 8.0f;

    animation.AddRagdollImpulse(new Vector3(0.0f, 2.0f, -8.0f));
}
```

The managed active-ragdoll API currently exposes `Enabled`,
`PositionStrength`, `RotationStrength`, and `Damping`. `MaxForce` and
`MaxTorque` remain editable and serializable through the Inspector and native
component API.

Useful lifecycle operations on `AnimationComponent` are:

- `RagdollEnabled`: creates or removes the physical ragdoll.
- `RagdollWeight`: blends the rendered pose from animation (`0`) to physics
  (`1`). It does not disable simulation or change controller strength.
- `AddRagdollImpulse(Vector3)`: distributes a world-space impulse across the
  bodies by mass. Calls made while ragdoll mode is disabled are ignored.
- `ResetRagdoll()`: discards the current physical instance and rebuilds it from
  the current animated pose on the next physics synchronization.

To implement a knockout without removing physics, disable the controller:

```csharp
controller.Enabled = false; // passive ragdoll
```

Re-enable it to let the skeleton physically recover toward the current entity
transform and animation.

## Tuning

Start with the defaults and tune in this order:

1. Set `RagdollWeight` to `1` so visual blending does not hide physical motion.
2. Adjust `MaxTorque` for the desired limb authority.
3. Increase `RotationStrength` until the pose is held adequately.
4. Increase `Damping` if the root overshoots or oscillates.
5. Adjust `PositionStrength`, then `MaxForce`, for locomotion following.

For a heavier character, use lower strengths or stronger damping. For a more
responsive character, raise strength gradually while keeping force and torque
bounds conservative. Large instantaneous changes to the entity transform ask
the controller to cover a large error and will naturally produce lag at bounded
settings.

## Troubleshooting

**The mesh remains animated and does not fall.** Verify that the mesh has an
imported skeleton, the Animation component is enabled, `RagdollEnabled` is
true, and `RagdollWeight` is greater than zero.

**The ragdoll is passive.** Ensure an enabled Active Ragdoll component is on the
same entity as the Animation component. The active controller is intentionally
optional.

**The character cannot keep up with its entity.** Increase `PositionStrength`
or `MaxForce`. If it overshoots after doing so, increase `Damping`.

**Limbs oscillate or look unnaturally rigid.** Reduce `MaxTorque` first, then
reduce `RotationStrength`. Active control must leave enough authority for
contacts and joint limits to affect the pose.

**The ragdoll starts in a bad pose.** Call `ResetRagdoll()` while the desired
animation pose is active. Also check the mesh bind pose and skeleton hierarchy;
the runtime bodies are derived from that data.

## Current limitations

- Collision capsules, masses, and joint limits are generated rather than
  authored per bone.
- The default angular limits are shared across joints rather than using a
  humanoid-specific profile.
- Active control uses one set of strength values for every joint.
- Managed scripting does not yet expose `MaxForce` or `MaxTorque`.
- `RagdollWeight` linearly blends skinning matrices; use `1` when evaluating
  physical correctness and reserve intermediate values for short transitions.

