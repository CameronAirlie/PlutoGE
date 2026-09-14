# Spline roads

M14 extends the existing Spline component. Set **GuardrailHeight** above zero in
the Inspector to generate continuous edge ribbons following the road's banking.
Zero preserves existing scenes. Heights are bounded to 0–10 world units in the
spline's local space. Guardrails currently share the road material and are thin
double-sided ribbons, with no posts or separate rail material.

The collision mesh now interpolates the authored control-point rotations, fixing
the previous mismatch where banked visual roads had flat collision cross-sections.
Guardrails are included in collision, use the same banked frames as the surface,
and persist through component serialization and existing scene history/prefabs.
Repeated rebuilds produce deterministic positions and topology.

## Local rebuilding and LODs

Roads cache CPU geometry per Catmull-Rom segment. Editing a control point or its
rotation rebuilds only the four affected segments (fewer at open ends). Changing
topology or geometry settings invalidates the whole cache. Material changes reuse
the geometry. Analytic tangents make adjacent segment cross-sections agree; UV
prefix distances are updated when assembling the road so edits do not leave stale
texture offsets. Collision geometry uses the same cache invalidation rules.

Each segment is a render submesh with independently selected LODs. **LodCount**
accepts 1–4, default 3; short segments may need fewer. LODs retain the exact end
cross-sections and reuse banked vertices. Screen-radius thresholds are 128, 64
and 32 pixels for the additional levels. Collision retains its configured sample
resolution. CPU rebuilds are local; the combined GPU buffers are still replaced
on the owning thread, so this does not promise constant-time edits on long roads.

## Authoring and export

- **Conform to Terrain** uses the existing Inspector terrain projection workflow.
  It samples the spline against terrain with configurable clearance and saves the
  resulting control points in one undoable edit. It is an explicit bake: repeat
  it after changing the terrain. Unhit points retain their original height.
- **Bake Roadside Prefabs** places a chosen prefab at fixed local arc distances,
  with an edge offset and optional placement on both sides. Output is a named
  child group containing ordinary prefab instances, including transform overrides.
  One operation supports at most 1,024 instances and is undoable. Repeated bakes
  create additional groups; remove or undo the old group when replacing it.
- **Export Road Mesh** writes a `.plutomesh` with geometry, all segment LODs and
  material references. Use a project reference such as
  `project://Meshes/Road.plutomesh`. Exported meshes use normal asset loading and
  cooking; procedural spline settings also persist through scenes and prefabs.
- **Road Junction** accepts 2–8 distinct open roads, choosing the start or end of
  each. Arrange their entrances as separate edges of a convex boundary, then use
  **Bake Junction Mesh**. The surface preserves the exact banked entrance vertices,
  inherits the selected road's visible material (including Mesh inspector assignments)
  and gains a mesh collider. Unassigned materials use the default shaded material. It
  rejects overlapping, collinear and interior entrances. The result is a mesh
  asset and a child of the selected road; undo removes the child, while the saved
  asset remains. Re-bake after editing the entrances. Junction undersides follow the connected roads' bottom edges, preserving their thickness and banking. Exposed boundary edges have
  side walls. Re-bake older junctions to add thickness. Junctions do not
  automatically redesign road approaches or add intersection guardrails.

Regression checks cover banking, rails, LOD bounds, deterministic local/full
rebuild equivalence, closed-seam invalidation, asset round trips, placement budgets,
prefab hierarchy undo/redo snapshots, junction rejection, junction collision and
material inheritance. The GPU mesh replacement regression forces address reuse
and checks that replacement geometry is uploaded while unchanged geometry is cached.
Interactive OpenGL/Vulkan visual checks and terrain/gizmo workflows still need a
playtest; automated geometry checks do not verify the rendered appearance.
