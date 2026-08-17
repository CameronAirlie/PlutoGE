# Render Performance Optimisation Plan

Constraint: preserve all post-process effects and visual fidelity.

- [x] Hierarchical cluster accept/reject culling using cached aggregate foliage bounds.
- [x] Renderer-owned reusable visibility scratch buffers, removing per-frame vector allocations
  without changing the public `RenderCommand` layout.
- [ ] Static GPU-resident instance data with a separate dynamic update path. *(In progress)*
- [ ] Persistent or ring-buffered shadow instance uploads.
- [ ] Cached static shadow draw preparation and per-cascade caster membership.
- [ ] Unified visibility, distance, projected-size, and LOD traversal.
- [ ] Optional GPU-driven culling and indirect-command generation.
- [ ] Renderer-wide OpenGL state caching.
- [ ] Pre-cached indexed uniforms without render-time name construction.
- [ ] Uniform buffers for shared camera, light, cascade, and effect constants.
- [ ] Full build, tests, profiler comparison, and visual-equivalence validation.

## Validation notes

- `git diff --check` passes for the implementation changes.
- The `PlutoGERender` RelWithDebInfo library rebuilt successfully after items 1 and 2
  (`PlutoGERender.lib`, 2026-08-17 11:46:05). The command runner timed out while MSBuild continued
  in the background, so validation uses the updated target artifact rather than its missing console exit status.
- A compact-index field was initially added to `RenderCommand`, but this exposed an ABI mismatch
  when the editor and engine libraries were rebuilt at different times. The field was removed and
  the original layout restored; pooled transform scratch storage now lives privately in `Renderer`.
