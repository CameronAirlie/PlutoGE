# Foliage and tree collision

PlutoGE foliage renders many copies of a mesh without creating one entity per
copy. Foliage types can optionally add simple static collision, making the same
system suitable for both non-colliding details such as grass and colliding
environment objects such as trees.

## Quick guide: paint colliding trees

If the tree is a prefab containing multiple static mesh components, export it
first:

1. Find the prefab in the Content Browser.
2. Right-click it and select **Export as Static Mesh**. The same action is also
   available in the selected asset's details area.
3. PlutoGE creates `<PrefabName>_Static.plutomesh` in the currently viewed
   Content Browser folder. If that name exists, a numeric suffix is added.
4. Assign the generated mesh to the foliage type. Every prefab mesh component
   appears as a submesh and retains its material.

The exporter includes enabled static `MeshComponent`s on the prefab root and
its active descendants. It bakes entity, mesh-offset, and submesh-offset
transforms into the generated vertices. Referenced materials remain shared;
inline material overrides are written as companion `.plutomaterial` assets.
Skeletal and animated-node meshes are rejected because the output is a static
mesh asset.

Then configure and paint the foliage:

1. Select an entity containing both `TerrainComponent` and
   `FoliageComponent`.
2. In the foliage inspector, add or select a foliage type and assign its tree
   mesh and material.
3. Open **Spatial Cells & Collision** for that type.
4. Enable **Enable Capsule Collision**.
5. Set **Collider Center**, **Collider Radius**, and **Collider Height** so the
   capsule surrounds the trunk. Keep the capsule tight and exclude branches
   and leaves.
6. Leave **Cell Size** at `32` initially. Smaller cells make local edits and
   physics rebuilds more granular, while larger cells create fewer Bullet
   objects.
7. Enable foliage painting, select **Add**, and paint the trees onto the
   terrain.
8. Enter play mode and test movement and raycasts against the trunks.

Collision is configured independently for every foliage type. Leave it disabled
for grass, flowers, and other small decorative meshes.

## Data model

`FoliageComponent` owns the placed instances and a collection of foliage types.
Each instance contains:

- A persistent 64-bit instance ID
- Local position
- Euler rotation
- Scale

The ID remains stable when other instances are removed. Old scenes whose
instance records do not contain IDs receive unique IDs when they are loaded.

A foliage type contains its render prototype and a `FoliageTypeAsset` policy.
The policy currently stores:

- Optional asset reference
- Spatial cell size
- Whether collision is enabled
- Local capsule center
- Capsule radius and height

The runtime mesh and material pointers are not stored in the policy asset. They
remain on the foliage type and are restored through their existing project
asset references.

## Foliage type assets

Reusable foliage policy files use the `PLUTOFOLIAGE` text format. The current
version is 1:

```text
PLUTOFOLIAGE	1
CELL_SIZE	32
COLLISION_ENABLED	1
COLLISION_CENTER	0	1.5	0
COLLISION_CAPSULE	0.4	3
```

The public C++ functions are:

```cpp
bool SaveFoliageTypeAsset(const std::string& path,
                          const FoliageTypeAsset& asset,
                          std::string* errorMessage = nullptr);

bool LoadFoliageTypeAsset(const std::string& path,
                          FoliageTypeAsset& asset,
                          std::string* errorMessage = nullptr);
```

Assign an existing project asset reference to a foliage type with:

```cpp
foliage->SetTypeAssetReference(typeIndex, "project://Trees/Oak.plutofoliage");
```

When an asset reference is present, the external asset is authoritative for
cell and collision settings when the scene loads. Inline scene properties are
still serialized as a fallback and for backward compatibility.

The editor currently exposes inline policy editing. Creating and selecting a
standalone foliage policy asset is a C++ or manual asset-file workflow until a
dedicated content-browser asset editor is added.

## Spatial cells and rendering

Instances are assigned to an X/Z cell from their local position:

```text
cell.x = floor(position.x / cellSize)
cell.z = floor(position.z / cellSize)
```

The configurable cell size is shared by render clustering and collision
partitioning. Render commands continue to use hardware-instanced model matrix
arrays, with one cluster per foliage type and cell.

The serialized source of truth remains the per-type instance array so existing
scenes and prefab overrides continue to work. Spatial collision data is derived
lazily and cached. It is regenerated only when:

- Instances are added, removed, or transformed
- A type's collision or cell configuration changes
- The owning entity's world transform changes

An ordinary frame does not rebuild the cell cache.

## Physics representation

Every populated collision cell becomes one static Bullet compound body. Each
foliage instance contributes one capsule child shape to that body. This avoids
creating an entity, rigidbody, and broadphase object for every tree.

The same representation is added to:

- The runtime Bullet dynamics world, for character and rigidbody collision
- The physics query world, for raycasts and other scene queries

Painting or editing foliage invalidates the affected component revision and
causes the physics representation to be refreshed. Static foliage bodies are
not transformed or updated every physics step.

Capsules are scaled from the complete owner and instance transform. Radius uses
the larger horizontal scale, while height uses vertical scale. Non-uniformly
scaled trees therefore retain a conservative trunk collider.

### Raycast results

`PhysicsRaycastHit` contains both identifiers:

```cpp
PhysicsRaycastHit hit;
if (scene.Raycast(origin, direction, distance, hit))
{
    // The entity that owns the foliage component.
    EntityID owner = hit.entityId;

    // Non-zero only when a foliage compound child was hit.
    std::uint64_t tree = hit.foliageInstanceId;
}
```

Ordinary entity collisions return `foliageInstanceId == 0`. Foliage instance
IDs are component-local handles and are intended for future selection,
promotion, damage, and persistence APIs.

## Choosing cell and collider settings

Start with these values and profile representative scenes:

| Setting | Suggested starting point | Tradeoff |
|---|---:|---|
| Tree cell size | 32 world units | Balanced body count and edit granularity |
| Dense forest cell size | 32–64 | Fewer Bullet objects, larger compounds |
| Sparse large trees | 16–32 | More precise spatial updates |
| Capsule radius | Trunk radius plus a small margin | Larger values feel disconnected from the mesh |
| Capsule height | Walkable trunk height | Avoid including thin canopy geometry |

Very small cells increase Bullet object and render-command counts. Very large
cells make a small foliage edit rebuild a larger compound and reduce culling
granularity. A cell should normally contain tens or hundreds of trees, not one
tree and not an entire forest.

## Serialization and compatibility

Current scene serialization adds the following per-type fields:

```text
Type.N.AssetReference
Type.N.CellSize
Type.N.CollisionEnabled
Type.N.CollisionCenter
Type.N.CollisionRadius
Type.N.CollisionHeight
```

Instance records now begin with their ID:

```text
id,position.x,position.y,position.z,rotation.x,rotation.y,rotation.z,scale.x,scale.y,scale.z
```

The loader still accepts the former nine-value transform record. It assigns a
new non-zero ID and saves the upgraded form the next time the scene is written.
Collision defaults to disabled, so loading an existing scene does not change
its physics behavior.

## Current scope

This milestone provides static capsule collision. It does not yet provide:

- Boxes, spheres, or multi-shape collider authoring
- Navigation obstacle generation
- Collision-layer settings per foliage type
- Destructible-tree promotion into entities
- A dedicated foliage asset editor
- Binary or streamed instance storage
- Per-cell render-command patching during an editor stroke

Render commands are cell-clustered and remain cached during ordinary frames,
but an edit currently invalidates the foliage component's complete render
command cache. Physics collision cells themselves are cached and revisioned.

## Relevant implementation files

- `engine/scene/include/PlutoGE/scene/FoliageTypeAsset.h`
- `engine/scene/src/FoliageTypeAsset.cpp`
- `engine/scene/include/PlutoGE/scene/components/FoliageComponent.h`
- `engine/scene/src/components/FoliageComponent.cpp`
- `engine/scene/src/Scene.cpp`
- `editor/ui/src/panels/InspectorPanel.cpp`
- `tests/FoliageCollisionTests.cpp`
