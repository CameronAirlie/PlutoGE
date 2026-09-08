# Exclusive VSM shadow path

The second editor capture reports 53.58 ms average frame time: 17.17 ms
geometry, 10.51 ms SSR, 7.06 ms voxel GI and 5.92 ms volumetric fog. It still
reports four cascade cache hits while VSM is active. Previously the renderer
maintained conventional cascades alongside VSM, and used them for missing VSM
pages and volumetric fog.

## Behaviour

- Select the effective shadow method before building cascade projections or
  allocating targets. Unsupported VSM devices/shader packages or draw counts
  above the VSM chunk capacity select CSM before either path runs.
- Active VSM performs no CSM projection fitting, target allocation, cache
  validation, object uploads or shadow draws. Switching to VSM releases existing
  CSM textures and per-cascade buffers. Switching back recreates and refreshes
  the conventional maps.
- The light component also releases its legacy dynamic/static/scratch cascade
  state when Virtual shadows are selected.
- Surface shading selects and filters only VSM pages while VSM is active.
  Four fine levels now have a fifth, coarse VSM level covering the outer fine
  level's world extent. Its 8x8 pages reserve 64 of the existing 256 atlas slots;
  they are requested every frame and new coarse pages receive update priority.
  Missing fine pages can sample this resident coverage without a CSM fallback.
- Fog and glass fog sample VSM visibility using the same coarse coverage,
  without separate screen-ray page requests. Updates share the existing bounded
  page/triangle budgets.
  Missing volume depth conservatively suppresses directional scattering, rather
  than inventing bright shafts. This can temporarily reduce shafts under budget
  pressure.
- Dirty resident pages retain their usable depth while updates are deferred.
  New or reassigned pages remain invalid until rasterisation completes. Under
  budget pressure shadows can temporarily be coarser or lag moving casters;
  cold starts and large projection changes still require residency warm-up.
- Light-space depth recentering has hysteresis, avoiding wholesale cache
  invalidation at small camera-motion quantisation boundaries. Signed zero is
  canonicalised before hashing the projection epoch.
- Voxel GI retains its independent volume-space injection shadow map. That map
  supports off-screen persistent GI and is not a camera cascade or VSM fallback.

Static pipeline layouts retain valid neutral texture descriptors for unused CSM
bindings. These refer to an existing 1x1 fallback texture, not shadow maps.
The profiler reports allocated cascade targets explicitly; it should show zero
targets, zero hits and zero updates while VSM is active. VSM indirect submissions
are still included in the general recorded-shadow-draw counter.

## Validation

The VSM-only rendering test checks surface visibility, fog with no opaque
receivers, CSM/VSM switching, disabling shadows, and absence of CSM resources
and work. The Vulkan test additionally checks legacy light-component state.
The existing shadow regression covers invalidation, alpha masks, deferred
updates and switching; deferred-page expectations now enforce VSM-only output.
The performance regression rejects cascade GPU scopes or extra cascade draws.
The movement regression warms an occluded receiver, then moves the camera and
caster for 24 frames with a one-page update budget. It requires continuous
shadow coverage, deferred updates, respected budgets and zero cascade work.
Reintroducing the old dirty-page invalidation makes this test fail with a
missing-shadow flash; the fixed shaders pass on Vulkan and OpenGL. The clipmap
test also checks depth hysteresis and eventual recentering.

```powershell
cmake --build out/build/gcc-profile --target PlutoGEEditor PlutoGEVulkanRhiTests PlutoGEOpenGLRhiTests -j 6
ctest --test-dir out/build/gcc-profile -R 'PlutoGE(VirtualShadowClipmap|OpenGLVsmOnly|VulkanRhi|VulkanVsmOnly|VulkanVsmPerformance|VulkanSsr|OpenGLSsr|VctWorldCacheRendering)Tests' --output-on-failure
out/build/gcc-profile/tests/PlutoGEOpenGLRhiTests.exe --shadows-only
out/build/gcc-profile/tests/PlutoGEVulkanRhiTests.exe --vsm-performance
```

The tests now load the fog shader on both backends and the OpenGL debug-output
shader, so their fog and shadow assertions actually exercise those outputs.
OpenGL now maps all RHI depth comparisons correctly: VSM receiver depth needs
`Greater`, and physical page clears need `Always`. Previously both silently
became `Less`, leaving VSM dependent on the conventional fallback.
Reprofile the original editor scene to measure total frame-time improvement.

## Measured results

The editor and both test executables built successfully in the `gcc-profile`
configuration. The seven targeted CTest cases above passed, as did the separate
OpenGL shadow suite (252 intermediate coverage levels, longest plateau 1 pixel).
The full OpenGL suite now passes its shadow section but subsequently fails the
translated-cloud bounds check with total difference 9815; cloud rendering was
not changed in this pass.

Initial exclusive-path pass, before the subsequent shadow-continuity changes:
local AMD Radeon(TM) Graphics, existing synthetic VSM benchmark at 582 x 507:

| Scenario | Before GPU frame | After GPU frame | Indexed commands before / after |
| --- | ---: | ---: | ---: |
| Stationary | 1.605 ms | 1.587 ms | 122 / 122 |
| Camera movement | 2.788 ms | 1.591 ms | 602 / 122 |
| Animated caster | 3.590 ms | 1.971 ms | 602 / 122 |

Moving-camera and animated-caster GPU time fell approximately 43% and 45% in
this benchmark. Stationary timing is essentially unchanged because the old CSM
cache already skipped raster updates there. These figures are not a measured
FPS gain for the user's full editor scene.

After the shadow-continuity fix, the editor rebuilt and all eight targeted
CTest cases passed, together with the separate OpenGL shadow suite. A fresh
synthetic run measured 1.770 ms stationary, 1.936 ms with camera movement and
2.642 ms with an animated caster, with 122 indexed commands in each case.
The coarse coverage adds VSM planning/refresh cost compared with the initial
exclusive-path pass; cascade work remains absent. The animated-caster case
retained usable shadow depth while deferring 126 page updates within budget.
