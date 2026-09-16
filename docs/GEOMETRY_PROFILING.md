# Geometry GPU diagnostics

GPU scope timing requires the Vulkan RHI backend. OpenGL supports the comparison
rendering modes but currently does not implement RHI GPU timestamp scopes.

In the editor Profiler's **Geometry diagnostics**, set **Comparison mode** to
**Automatic shading sweep**. Keep the camera, scene, resolution, and other settings
fixed (pause animation where possible), and retain at least 240 rendered frames.
The sweep cycles seven variants, spending 32 frames on each (224 frames per cycle).
Appearance changes intentionally while it runs. Return to **Normal** afterward.

Compare the named **RHI Geometry sweep / ...** GPU timings in the capture:

| Variant | What it measures relative to Normal |
| --- | --- |
| Minimal shading (coverage preserved) | Removes material sampling and lighting for standard opaque surfaces; retains vertex processing, rasterization, depth tests, motion and G-buffer writes. Masked surfaces and fragment graphs still evaluate materials to preserve coverage. |
| Material only (no lighting) | Retains material textures and graphs but removes surface lighting. |
| Bypass directional shadows | Isolates directional shadow sampling in geometry, not shadow-map generation. |
| Bypass standard detail textures | Skips standard normal, metallic and roughness maps. Graph materials retain their inputs. |
| Bypass point lights | Skips point-light shading. |
| Bypass sky lighting | Replaces physical sky ambient lighting with the constant ambient fallback. |

These are controlled shader comparisons, **not additive sub-timings**. GPU work
overlaps and resource bottlenecks can shift. A small delta does not establish that
the disabled work has zero cost. Each geometry scope still includes common work,
including outlines; transparent rendering runs later. Material/minimal/detail/point/sky
diagnostics target opaque and alpha-tested shading. Directional-shadow bypass also
affects transparency. Check the existing opaque/outline scopes in a
normal capture to identify which category dominates.

Sweep scope names are assigned when recording GPU commands, so asynchronous query
results retain the correct variant. Ordinary nested scopes pool samples from all
variants during a sweep. Do not compare their averages or downstream post-process
timings as if the scene were unchanged: exposure and temporal history see the
changing diagnostic images. Re-run an individual comparison mode for longer,
stable measurements before deciding on an optimization.

**Draw range timings** preserves shading and order while adding at most eight GPU
scopes over consecutive ranges of the input draw list. Their inclusive indices
appear in the scope names. This locates expensive groups without exhausting the
timestamp pool with one query pair per object. These scopes cover opaque and
alpha-tested submission; transparent draws are queued separately. Parent scopes
include child timings and must not be added to them. Extra queries have overhead.

If minimal shading remains close to Normal, investigate vertex/primitive work,
coverage, depth rejection, and G-buffer bandwidth. If material-only is much faster,
use the lighting bypass comparisons to narrow the lighting cost. If minimal is
much faster than material-only, investigate textures and material graphs. Confirm
any proposed optimization with Normal-mode captures of the same scene.
