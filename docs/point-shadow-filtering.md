# Point-light shadow acne

Point lights use six 512x512 perspective faces in the existing point-shadow
atlas. Directional VSM is a separate path. On a horizontal plane the downward
face covers a square beneath the light; outside it, side faces see the receiver
at an oblique angle. Comparing all nine filter taps against the fragment's
single depth produces self-shadowing that changes abruptly at that square.

The point-shadow filter now derives the geometric receiver plane before
per-light branching. For each sample it reconstructs the cube ray, resolves the
correct face, snaps to that face's actual texel centre, and intersects the ray
with the receiver plane. The resulting projected depth is compared with the
stored depth using only a small precision bias. Samples crossing a face edge
continue onto the neighbouring face instead of being clamped to the old face.
Normal-map shading perturbations do not affect the plane. The filter retains
nine depth samples and the existing atlas allocation/render passes.

The shared Vulkan/OpenGL particle-and-point-light test compares shadowed and
unshadowed versions of a self-casting plane in all six orientations, at two
world scales. A separate blocker straddles the downward/side-face boundary to
ensure genuine occlusion remains. Existing tests cover light range, disabling
shadows, alpha-masked and instanced casters.

The original shader reproduces up to 86/255 brightness loss on the close-light
plane. The corrected Vulkan and OpenGL shaders produce zero loss across all twelve
orientation/scale combinations while retaining the boundary-crossing blocker.
The precision bias is 0.000001 in projected depth; a smaller 0.0000002 value
was insufficient for raster/interpolation rounding in the close-light case.
The full Vulkan RHI and directional-VSM regression tests also pass.

```powershell
ctest --test-dir out/build/gcc-profile -R 'PlutoGE(Vulkan|OpenGL)ParticlePointTests' --output-on-failure
```

## VSM decision

No point-light VSM migration is included. VSM can support point lights through
virtual cube maps, as described in [Epic's VSM documentation](https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine).
For this engine, that would require perspective cube-face page requests,
per-light cache identities and shared residency/update budgets; its current VSM
implementation is for directional clipmaps. It would still need correct
receiver-plane comparisons. The recommendation for this acne issue is to fix
the current sampler first, and evaluate a separate VSM migration against
measured point-light resolution and rendering-cost requirements.
