# Spline roads: current delivery

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

Regression checks cover banking consistency between visual/collision meshes,
guardrail collision geometry, deterministic rebuilds and serialization/removal.
M14 remains partial. Junctions, terrain conformity, roadside prefab placement,
LODs, local section rebuilds and dedicated asset/export workflows are outstanding.
