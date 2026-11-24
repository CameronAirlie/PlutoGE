# Texture streaming: assessment (2026-11-24)

Should PlutoGE stream textures (load mip levels by need, within a memory budget)? Short answer: **viable, but not
the next thing to build.** The cheaper steps that streaming depends on fix the problems actually observed. Mip
streaming itself pays off only once memory is constrained, which it is not on the target machine today.

## What happens today

| Stage | Behaviour | Where |
| --- | --- | --- |
| Decode | stb_image (or `DecodeDds`, base level only, BC decompressed back to RGBA8), synchronously on the calling thread, normally the main thread during scene and material loading | `TextureManager.cpp:258`, `DdsImage.h`, `AssetManager.cpp:167` |
| CPU copy | `render::Texture::m_rgba8Pixels` keeps the full-resolution RGBA8 base level for the texture's lifetime | `Texture.h:87` |
| Cooking | Content packs copy source files; there are no cooked mip chains or GPU formats | `docs/CONTENT_PACKS.md` |
| Upload | On first use, the whole mip chain at once (GPU blits); normal-map mips on a worker | `RhiSceneRenderer.cpp:666-783` |
| Compression | BC encoded on worker threads on the player's machine, cached under `%TEMP%`, swapped in after the RGBA8 upload | `RhiTextureCompression`, `docs/TEXTURE_COMPRESSION.md` |
| Residency | `residencyRevision` only re-prepares materials when a texture handle is replaced; nothing evicts, drops mips or reads a budget | `RhiSceneRenderer.h:464-487` |
| RHI | No per-mip upload (`TextureRegion` has no level), no min-LOD clamp on views or samplers, no sparse images; `VK_EXT_memory_budget` requested from VMA but never queried | `rhi/Types.h:341`, `VulkanDevice.cpp:5083, 5360, 4807` |

Terrain layer arrays (up to 2048² × layers × 2 arrays) and impostor atlases stay RGBA8.

## SurvivalGame's numbers

- Source textures: 187 files (92 PNG, 39 JPG, 56 TGA; 860 MB on disk). Mostly 1024² and 2048², with seven 4096².
- Fully resident as RGBA8 with mips that is about **1.6 GB**, plus the same base levels again as CPU copies. As BC
  (1 byte per texel) it is about **0.4 GB**.
- LakeDistrict benchmark (camera travelling, Low): peak **4.4 GB private / 2.9 GB working set**. The machine has 60 GB
  of RAM and the Vega 8 has a 4 GB carve-out, so nothing was evicted or paged because of textures.
- GPU cost does not depend on texture resolution here: capping the terrain layer arrays from 2048² to 1024² changed
  the opaque pass by under 0.1 ms. Mipmapping already reads small levels at distance.

So on this machine streaming would not raise the frame rate. It would cut memory, which matters for players with
8–16 GB machines (an iGPU shares that RAM), and for load time.

## What the observed problems need instead

1. **Decode on worker threads.** Synchronous stb decode on the main thread is a load-time and first-sight cost
   (a 4096² TGA is a 64 MB decode). Decoding where the tile and scene loaders already use workers removes it.
2. **Drop the CPU copy after upload** where nothing on the CPU reads it (keep it for terrain weights, impostor bakes,
   readbacks). For SurvivalGame those copies are about 1.2 GB of RGBA8 base levels, more than the BC GPU textures
   themselves.
3. **Cook BC mip chains into content packs.** This moves today's per-player first-run encode to build time, and
   gives files whose mip levels can be read independently (`Pack::Read` already supports byte ranges).

## Mip streaming on top of that

Once there are cooked per-level chains, streaming needs:

- **Demand:** a per-texture wanted mip. The cheap version is the screen-space size of each draw's bounds times the
  material's texel density, taken in `RhiSceneRenderer::Prepare`, where draws are visited anyway. Sampler feedback
  is not available on this RHI.
- **Upload:** a single-mip upload into an existing image (`TextureRegion` gains a level). Add a min-LOD clamp
  (Vulkan view `baseMipLevel` or sampler `minLod`; GL `GL_TEXTURE_BASE_LEVEL`/`MIN_LOD`), or recreate the image and
  swap it through the existing retire and `residencyRevision` path. The recreate route needs no new RHI concept and
  reuses tested machinery.
- **Budget:** query `vmaGetHeapBudgets` (and enable `VK_EXT_memory_budget`) to drop the finest levels of the
  least-recently-needed textures when over budget.

Rough size: demand and budget logic plus the recreate-and-swap route is about a week. Cooked mip chains in the
pack format and per-level reads add about another week, with tests on both backends.

## Recommendation

Do steps 1–3 first. They help every machine, have no visual cost, and are prerequisites anyway. Then add mip
streaming with recreate-and-swap and a budget, if a low-memory target (8 GB machines, or a 2 GB carve-out) becomes
a goal. Revisit sparse images only if recreate-and-swap churn shows up in profiles.
