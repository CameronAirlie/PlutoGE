# Surface response assets (M09)

Surface assets describe gameplay contact responses independently of rendered
materials. A `.plutosurface` contains a friction coefficient and separate footstep
and impact entries, each with optional sound, particle-system, and decal-material
references.

## Authoring

1. In the Content Browser, use **Create > Surface Response** and enter a name.
2. Select the asset. In its details, choose the sound, particle system, and decal
   material for each event. Set friction, then click **Save Surface**.
   **Revert Unsaved Changes** discards the current draft. Selecting another asset
   also discards unsaved edits.
3. On a Collider component, select **Surface Asset**. This works with box, sphere,
   capsule, mesh, and terrain colliders. One surface applies to the entire collider;
   terrain texture layers do not select different surfaces.

Assignments use normal scene history, prefab overrides, duplication, and play-mode
snapshot restoration. Shared asset files use explicit Save/Revert; editing their
contents is not a scene undo operation. The common-value inspector also exposes
the serialized `Surface Asset` field for multiple selected colliders.

An assigned surface sets Bullet contact friction for static and rigidbody
colliders. Without an assignment, existing rigidbody friction and Bullet's static
default remain in use. An assigned missing or invalid asset resolves to friction
`0.5` and empty effects. Coefficients must be finite and between `0` and `10`.
Custom scripted character movement remains responsible for its own traction.

## Runtime access

Native code calls `Scene::ResolveSurfaceResponse(entityId, &loaded)` and selects
`asset.GetResponse(assets::SurfaceEvent::Footstep)` or `Impact`. Disabled or missing
colliders return defaults. The collider belongs to the hit entity; the resolver
does not search its parents or infer a surface from a rendered material.

Managed code can resolve a raycast directly:

```csharp
if (Physics.Raycast(origin, direction, 100, out var hit))
{
    var response = SurfaceResponses.Resolve(hit, SurfaceEvent.Impact);
    if (response.DecalMaterial.Length > 0)
        Decals.Spawn(hit, response.DecalMaterial, new Vector2(0.15f),
            lifetime: 10, fadeDuration: 2);
}
```

`SurfaceResponses.TryResolve(entity, kind, out response)` returns whether an asset
was found and supplies defaults on failure. Resolve does not spawn effects:
gameplay controls timing, volume, particle counts, decal size, and lifetime.

## Working footstep example

Add the supplied `PlutoGE.ScriptCore.Examples.SurfaceFootsteps` script to a moving
character. Add a Sound Emitter with **Play On Awake** and **Looping** off. Optionally
add a Particle System with **Play On Awake** off. Set `groundDistance` to reach from
the character origin to the ground, and `stride` to the desired travel distance
between footsteps. Assign a surface to the ground's collider and fill its footstep
responses. Move the character: grounded horizontal travel triggers one-shot audio,
particles at the raycast contact, and fading footprint decals. Stationary and
airborne characters do not emit footsteps.

For impacts, use `SurfaceEvent.Impact` at the collision or weapon raycast and play
the returned references through the same sound, particle, and decal APIs.

## Persistence and cooking

The versioned text format uses `key=value` lines. Spaces in references are retained.
The loader rejects wrong asset types, invalid numbers, duplicate keys, unsupported
versions, and malformed records. Parsing failures leave the caller's output intact.
Surface assets and failures are cached per project; editor saves replace cache
entries immediately. External file edits require reopening the project.

The shared dependency scanner recognizes `.plutosurface` references. Pruned cooking
follows scene/prefab collider assignments through both response entries and their
transitive particle/material/texture dependencies. Cooked files retain project
references and load through the runtime asset manager.

Native regression tests cover format validation, defaults, actual Bullet sliding
friction, all collider assignment paths, snapshot restoration, prefab duplication,
project cache isolation, and pruned cooking. Managed tests cover the native ABI,
UTF-8 strings, event routing, and fallback behavior. Interactive audio and graphics
verification should use the example above on the target renderer.
