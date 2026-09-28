# Shadow and transparency performance phase

## Plan and capture baseline

The 240-frame editor capture averaged 22.11 ms CPU and 11.73 ms scene GPU.
The inclusive Shadows CPU scope averaged 9.19 ms, including frames with no VSM
submissions. Transparency averaged 5.95 ms GPU; example frames copied the scene
218 times for 218 panes. These measurements motivate four targeted changes:

1. Cache unchanged point-light shadows.
2. Separate shadow preparation, recording, and point-light GPU measurements.
3. Reuse immutable VSM cluster bounds.
4. Group glass copies using actual read-after-write dependencies.

## Implementation

Point shadows use a content signature of active face projections and eligible
casters. Geometry revisions, transforms, alpha state, and graph inputs contribute
to invalidation. Time/view-dependent graphs include the appropriate
frame inputs. A hit records no point-shadow draws or object/material uploads.
The entire atlas is refreshed on a miss: the existing attachment clear applies
to the whole atlas, so skipping individual faces would erase cached contents.
Removal of all casters still clears and publishes an empty atlas. Renderer
shutdown invalidates the cache. In-place resource edits must continue to advance
the engine's packet/mesh revision contract.

VSM keeps one bounded cluster-plan entry per caster. Immutable packet and mesh
revisions identify reusable merged bounds. Projection-dependent page fit is
reevaluated each frame, allowing light/clipmap movement without stale decisions.
Draws without packet revisions rebuild once per frame. The preparation pass
uses the same plan as capacity estimation, avoiding duplicate merging.

Glass planning distinguishes conservative raster bounds from expanded sampling
bounds. A later pane can share a snapshot if its samples do not intersect any
earlier pane's raster footprint. Read/read overlap is safe. Back-to-front order,
unknown/deformed-bound fallback, and the 64-pane planning cap are preserved.
Genuinely overlapping layers still require independent snapshots.

Profiler traces expose Shadow signatures, VSM preparation, VSM recording,
Point shadow cache validation, and Point shadow recording CPU scopes. GPU work
has an RHI Point Shadows scope. Reports and the panel expose point-atlas hits,
updates, and draws; reports also expose VSM cluster-bound builds and hits.
GPU observations remain asynchronous and must not be aligned to CPU frames.

## Validation

Regression checks cover point-only caching and image equality, moving casters
and lights, mesh deformation, alpha edits, and caster removal. Glass checks
compare a shared snapshot against independent full snapshots byte-for-byte,
including overlapping sample footprints with no raster dependency. VSM checks
assert immutable bounds reuse and revision-triggered rebuilds.

The original Bistro editor scene must be recaptured to quantify real-scene
improvements. Synthetic tests establish correctness and eliminated work, not a
promised frame-time reduction. Atlas-wide invalidation and conservative glass
bounds remain deliberate limitations for future per-face caching and improved
imported bounds.

Validated in Release on the RTX 4070 SUPER:

- Editor, runtime, Vulkan tests, and OpenGL tests built successfully.
- Full Vulkan and OpenGL suites passed, including the new point/graph and glass checks.
- VSM performance/revision checks passed; stationary frames recorded one scene
  draw and zero shadow triangles, with 128 page hits and zero deferred pages.
- CPU clipmap policy checks and `git diff --check` passed.

Build/test logs are under `out/build/shadow-next-*`. No Bistro frame-time gain
is claimed until a new editor capture is collected at matching settings.
