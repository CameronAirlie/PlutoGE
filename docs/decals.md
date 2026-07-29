# Decals

PlutoGE decals are box-projected into the deferred G-buffer between opaque
geometry and lighting. They receive scene lighting without requiring a mesh and
are clipped by both the projector volume and the receiving surface normal.

## Bullet impact

Use the physics hit directly:

```cpp
PlutoGE::scene::PhysicsRaycastHit hit;
if (scene.Raycast(muzzlePosition, shotDirection, 1000.0f, hit, playerEntityId))
{
    scene.SpawnDecal(
        hit,
        "Materials/BulletHole.pgemat",
        glm::vec2(0.12f),
        0.06f, // projection depth
        30.0f, // lifetime; zero means permanent
        2.0f); // fade duration
}
```

The helper places the projector just above the hit point and aligns its local
Z axis with the hit normal. The returned entity can be parented, tagged, tinted,
or destroyed like any other entity.

## Authored decals

Add a `Decal Component` to an entity in the inspector, select its material asset,
and use the entity scale as `(width, height, projection depth)`. The material's
albedo map should normally contain transparency and use Mask or Blend alpha mode.
`Normal Cutoff` prevents projection across sharp
corners: `1` accepts only parallel surfaces, `0` accepts surfaces within 90
degrees, and `-1` disables angle rejection.

`Lifetime` of zero keeps the decal indefinitely. For temporary decals,
`Fade Duration` controls the alpha fade at the end of the lifetime.

## C# scripting

The scripting API accepts either a raycast hit or an explicit point and normal:

```csharp
if (Physics.Raycast(origin, direction, 1000.0f, player, out var hit))
{
    GameObject? decal = Decals.Spawn(
        hit,
        "Materials/BulletHole.pgemat",
        new Vector2(0.12f, 0.12f),
        depth: 0.06f,
        lifetime: 30.0f,
        fadeDuration: 2.0f);
}
```

`KinematicFpsController` exposes `bulletHoleMaterial`, `bulletHoleSize`,
`bulletHoleDepth`, `bulletHoleLifetime`, and `bulletHoleFadeDuration`. Assigning
the material enables bullet holes; leaving it empty disables them. The controller
reuses its weapon raycast for damage and decal placement.

Material-reference strings can use `[MaterialAsset]` alongside
`[SerializedField]` to get a filtered material dropdown and material
drag-and-drop support in the script inspector.

## Current scope

The pass currently consumes the material's base color, albedo map, UV scale, and
alpha settings, and modifies G-buffer albedo only. This is intentional for the first implementation:
it covers bullet holes, dirt, paint, blood, and signage while keeping the
material contract simple. Normal, roughness, metallic, and emissive decal
channels can later be added to the same command and pass without changing the
scene-facing API.
