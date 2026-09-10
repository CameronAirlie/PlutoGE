# ShadowSouls render performance investigation

The supplied capture (frame 314791) reported 5.42 ms CPU, 3.82 ms viewport rendering, and an asynchronous 2.52 ms scene GPU observation. Geometry accounted for 343 draws / 343 instances and 0.51 ms CPU recording. Particle component updates accounted for 0.25 ms; render recording had 1.945 ms of self time that did not distinguish particle preparation. These values are not a measured FPS result, and CPU and asynchronous GPU observations must not be added together.

Main.plutoscene contains 346 mesh components using only 13 mesh/material/submesh combinations. The RHI translator forwarded every ordinary mesh as an individual draw even though BasicRenderer already supports instanced geometry.

## Changes

- Batch compatible visible opaque BasicDraw packets by geometry, LOD and complete material state. Keep per-object current and previous transforms so movement and temporal reconstruction survive changes in visibility and batching. Existing instances, transparent/glass surfaces, skinned sources and packets without explicit history stay separate. Shadow and GI source lists retain their original packets for cache validation. Hash all batch state and check equality within hash buckets.
- Skip empty particle render packets, reserve the live-particle sorting list, and share camera matrix calculations across emitters.
- Calculate particle rotation sine/cosine and sprite random value once per particle rather than for each of its six vertices. Do not select/sort smoke lights for unlit effects.
- Skip empty CPU particle simulation work, calculate drag damping once per system update, and avoid evaluating turbulence when its strength is zero.
- Add the `Particle render preparation` CPU trace scope beneath `Render recording`.

No scene quality settings or particle counts were reduced.

## Validation

Builds completed: `msvc-distribution` Release editor/runtime and both RHI tests; `msvc-nvidia` Debug editor/runtime and Vulkan RHI tests. The rebuilt NVIDIA editor is at `out/build/msvc-nvidia/editor/Debug/PlutoGEEditor.exe`. Installed Program Files binaries are not replaced.

The new `--opaque-batching` rendering test exercises 343 objects with four materials. It checks material/LOD separation, transparent and existing-instance exclusions, previous-transform preservation, identical image output and instanced draw counts.

| Backend/build | Geometry draws | Geometry CPU before / after | Batch preparation CPU | Maximum pixel difference |
| --- | --- | --- | --- | --- |
| Vulkan Release | 343 -> 8 | 0.0754 / 0.0110 ms | 0.0257 ms | 0 |
| OpenGL Release | 343 -> 8 | 0.1399 / 0.0337 ms | 0.0254 ms | 0 |
| Vulkan NVIDIA Debug | 343 -> 8 | 0.6784 / 0.0465 ms | 0.1911 ms | 0 |

Geometry measurements average 80 frames after 20 warm-up frames in a synthetic 96 x 64 render test. Batch preparation averages 200 calls and excludes copying the input test vector. Including that preparation, the tested CPU work drops from approximately 0.0754 to 0.0367 ms in Vulkan Release and 0.6784 to 0.2376 ms in Vulkan Debug. These are not before/after measurements of the user's full arena capture, and do not measure full scene translation, GPU time or FPS.

The NVIDIA Debug focused test passed with Streamline reporting an unavailable/untrusted interposer at the test executable location; this test does not use DLSS and does not validate DLSS behavior.

Both backends also passed the particle/point-light rendering checks and temporal-motion checks. A ten-second hidden runtime startup of the actual ShadowSouls project initialized gameplay and HUD without stderr output.

## Recheck in the editor

Open ShadowSouls in the rebuilt editor and capture the same view, resolution, play state, upscaler and VSync settings. Compare `RHI recorded geometry`, `RHI geometry recording CPU`, `RHI command translation`, `Particle render preparation` and total CPU time over multiple frames. Active hit flashes can legitimately split a material group. GPU measurements remain asynchronous. The original capture had a debugger attached; use the same configuration for an A/B comparison, then assess shipping performance in Release separately.

Focused test commands:

```powershell
ctest --test-dir out/build/msvc-distribution -C Release --output-on-failure -R 'OpaqueBatching|ParticlePoint|TemporalMotion'
```
