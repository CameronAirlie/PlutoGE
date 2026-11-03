# Cascaded shadow caching and caster culling

Cascaded shadow maps (CSM) redraw a cascade only when its inputs change: its shadow matrix, resolution, minimum caster
size and the signatures of the casters inside it (`BasicRenderer`, cascade input and content signatures). Four
mechanisms keep those inputs stable or small. They apply in the RHI renderer, which is also the VSM capacity fallback.

## Producer shadow distance

`RenderCommand::maxShadowDistance` (foliage layers' **Max Shadow Distance**) drops a command from every shadow list
when its bounds lie wholly beyond that distance from the camera (`RhiSceneRenderer`, `WithinShadowDistance`). Whole
commands are dropped (a foliage cell), so the bound is conservative: a cell keeps casting while any part of it is in
range. Materials whose graph displaces vertices are exempt, as in the legacy `ShadowPass`. The filter runs before the
VSM capacity check, so a map of short-shadowed foliage can fit the VSM instead of falling back to CSM. The profiler
reports the count as **RHI shadow caster culling: ... beyond their shadow distance**.

## Cascade retention

`DirectionalShadowCascadeRetention` (`DirectionalShadowCascades.h`) keeps each cascade's previous projection for as
long as it still covers the camera's slice: the slice's eight corners inside its edges less the filter guard, and the
casters toward the light within its depth range (`CoversDirectionalShadowSlice`). A different light direction,
resolution or slice size refits. Projections snap to a texel grid anchored at the world origin, so a retained one
samples the world on the same texels as a fresh fit; only the map's placement differs. Its matrix then stays the
same, the cascade's cache holds, and the cascade is redrawn when its casters change rather than whenever the camera
moves. `PlutoGEDirectionalShadowCascadesTests` checks coverage, texel phase and refits while walking.

**Project Settings > Rendering > Performance > Retain shadow cascades** (`RETAIN_SHADOW_CASCADES`, on by default)
switches it. The profiler reports **projections retained** on the cascade cache line.

## Small caster culling

Each redrawn cascade skips casters whose largest instance spans fewer than **Smallest shadow caster** texels across
(`SHADOW_CASTER_MIN_TEXELS`, default 1; `BasicLighting::shadowCasterMinimumTexels`). The size is
`BasicDraw::maxInstanceRadius`: the geometry's bounding radius under the draw's largest instance scale, computed
once when the packet is prepared. Below a texel a caster only adds aliasing under the filter; grass in the far
cascades is the common case.

## Draw signatures

Each shadow draw's signature (mesh, range, material lanes, bounds and instance matrices) is cached by the immutable
packet it describes (`BasicDraw::preparationRevision`), not by its position in the caster list: the list gains and
loses draws as the camera moves, and a positional cache missed for every draw after the change, rehashing each one's
instance matrices (60 ms a frame on the Verdant jungle). Instance arrays hash a word at a time in four lanes
(`HashVctArray`). Entries unused for 240 frames are dropped.
