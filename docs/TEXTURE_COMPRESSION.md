# Texture compression

Material textures are sampled from block-compressed copies (BC1, BC3 and BC5) instead of RGBA8. A compressed texture
takes a quarter (BC1) or half (BC3, BC5) of the memory, and every sample reads correspondingly fewer bytes. On an
integrated GPU, which shares system memory bandwidth with the CPU, that bandwidth is the main win.

Source images are unchanged. The engine decodes them to RGBA8 on the CPU ([Texture loading](#texture-loading-and-cpu-pixels)),
and the renderer encodes what it uploads, unless the game's export already did ([Cooking](#cooking)).

## What is compressed

| Use | Format |
| --- | --- |
| Albedo, emission, sheen colour (sRGB), opaque | BC1 sRGB |
| Same, with any alpha below 255 | BC3 sRGB (BC7 when a BC7 encoder is registered) |
| Metallic, roughness, occlusion, clear coat, transmission, emission masks, opaque | BC1 |
| Same, with alpha | BC3 (BC7 when registered) |
| Normal maps | BC5 (X and Y; shaders rebuild Z) |

Kept RGBA8: textures under 128x128 texels, shader-graph textures (their meaning is unknown to the renderer), impostor
atlases, terrain weight maps (painted in place) and terrain layer arrays, particles, decals, render textures and UI.
A texture used both by a compressed slot and by one of these keeps RGBA8 for both.

Devices report support per format (`rhi::IRenderDevice::SupportsSampledFormat`): Vulkan needs
`textureCompressionBC`; OpenGL needs `EXT_texture_compression_s3tc` for BC1/BC3 (and `EXT_texture_sRGB` for their
sRGB forms; BC4, BC5 and BC7 are core). Anything unsupported stays RGBA8.

## When it happens

- **Exported games** find their material textures already encoded in their content pack ([Cooking](#cooking)), and
  encode only what the cook could not foresee.
- **Runtime and editor.** The first frame that samples a texture uploads RGBA8 as before, and queues an encode on
  background threads. When the encode finishes, the compressed texture replaces the RGBA8 one and the materials that
  sample it re-prepare. Nothing waits for compression.
- **Captures and tests** (`RhiSceneRenderer::SetImmediateTextureUploads(true)`) encode before first use, so images do
  not depend on timing.
- **Cache.** Encoded chains are stored by content under `%TEMP%/PlutoGE/TextureCompression`, keyed by the source
  texels, size, use, format and encoder. Each texture is encoded once per machine; later runs load it. Entries are
  never invalidated by time: a change to any input is a different key. Deleting the directory is always safe.
- **Memory bound.** Queued encodes hold a copy of their source texels, capped at 256 MB
  (`TextureCompressionSettings::maxPendingBytes`). Textures beyond it keep RGBA8 until earlier encodes finish.

## Cooking

Exporting a game (`ExportStandaloneProject`, `PlutoGERuntime --export`) encodes its material textures into the content
pack, under `Cooked/TextureCompression` (`render::kCookedTextureDirectory`), in the cache's own entry format. The
runtime host adds that directory to `TextureCompressionSettings::cookedDirectories`, read-only layers the store searches
(through the content layer, so inside the mounted pack) before its writable cache. Players then start with every
material texture encoded, rather than spending their first sessions encoding on background threads.

- `assets::CookMaterialTextures` (`assets/TextureCook.h`) reads every `.plutomaterial` in the staged export, resolves its
  texture slots as material loading does (`AssetManager::ResolveMaterialTexturePath`, through the export's runtime
  catalog, so asset IDs resolve), decodes each file once (`render::DecodeImageFile`, the decoder the runtime uses) and
  encodes it for every way a material samples it.
- How a slot is sampled comes from the slot table (`render::GetMaterialTextureSampling`): sRGB slots as colour, the
  normal slot as tangent normals, the rest as data, emission masks as data.
- The format comes from `render::SelectTextureCompression`, which the renderer's own choice also goes through, for a
  desktop target (`DesktopBlockCompressionSupport`: every BC format). Entries are keyed by content, semantic, format and
  encoder, so a device or a newer encoder that would choose differently simply misses and encodes as before; a cook
  never makes the image differ from an uncooked run.
- `ExportOptions::cookTextures` (on by default; `--no-texture-cook`) turns it off.

SurvivalGame (2026-11-24): 154 entries, the pack 139 MB larger (1,048 vs 909 MB). A first launch with an empty
cache encoded 1 texture instead of 81. Only textures that material files name are cooked; one sampled through another
path (detail grass ground textures, decals) is still encoded on first use.

## Texture loading and CPU pixels

- **Decoding off the owner thread.** `render::DecodeImageFile` (`ImageDecoding.h`) reads through the content layer and
  decodes (stb_image, DDS) on any thread. `TextureManager::LoadTextureFromFile` decodes through `LoadingWork::Prepare`
  (a loading screen keeps presenting), or takes a decode started ahead of time: material loading starts its textures'
  decodes together (`AssetManager::PrefetchTextures`, `TextureDecodeQueue`), so they decode side by side on workers.
  Loads still return complete textures.
- **Releasing CPU pixels.** A file texture's pixels can be dropped once the renderers hold its GPU copies: a renderer
  reports `Texture::MarkUploaded` when an upload's compression is settled (queued with its own copy, done, or not
  wanted), and under `TexturePixelRetention::ReleaseAfterUpload` `TextureManager::ReleaseUploadedPixels` (called each
  frame by `Engine::BeginFrame`) releases them. Anything that reads them later gets them back from the file
  (`Texture::SetPixelSource`). The standalone runtime releases (`PLUTO_TEXTURE_PIXELS=keep` keeps them); the editor
  keeps every texture's pixels for thumbnails, previews and painting. Generated and painted textures (no file) always
  keep theirs.
- **Threads.** Owner-thread code reads `GetRgba8Pixels()` (valid until the owner next replaces or releases them).
  Code on other threads (the render thread's pixel reader, UI and portrait renderers) uses `AcquireRgba8Pixels()` or
  `CopyRgba8Bytes()`, whose shared ownership keeps the pixels alive through a release.
- SurvivalGame's LakeDistrict (camera travelling, Low): late-run private memory 3.5-3.8 GB with release against
  4.1-4.2 GB kept; frame times equal and no pixel read above 0.16 ms (nothing was read back during play).

## Replaced textures

Swapping an upload (RGBA8 to compressed, or back for an exact use) must not destroy a texture something may still
bind. Renderers sharing a device's texture cache prepare and record on their own schedules, so a replaced upload is
retired (`TextureCache::retired`): it is released only after every live renderer has prepared its materials at or
past the residency revision that replaced it (`TextureCache::preparedResidency`), and the device has completed the
submission pending at that point.

Progressive VCT updates keep copies of GI draws across frames (`BasicRenderer` cascade `pendingDraws`). They now record
the textures those draws bind and abandon the update when one is no longer bound by any live draw, as they already did
for meshes; otherwise they bound the released texture (`Invalid Vulkan texture binding`, every frame, in Verdant).
Vulkan binding errors now name the texture handle and the bound pipeline.

## Settings

`RhiSceneRenderer::SetTextureCompression(TextureCompressionSettings)` (shared by every renderer on a device):
`enabled`, `minimumTexels`, `cacheDirectory` (empty disables the cache), `workerCount`, `maxPendingBytes`.
`PLUTO_TEXTURE_COMPRESSION=0` disables compression in every process. `RhiSceneTimingStats` reports
`compressedTextureUploads` and `pendingTextureCompressions`.

## Normal maps

BC5 keeps two channels at full precision. `BasicDraw::normalEncoding` tells shaders which layout a normal texture
has (`NormalTextureEncoding::Rgb` or `Xy`): the bound path passes it through `MaterialParameters::hasNormalTexture`
(0 none, 1 RGB, 2 XY), and bindless material records through `BindlessMaterialNormalXy`. `decodeMaterialNormal` in
`BasicLit.slang` rebuilds Z as `sqrt(1 - x^2 - y^2)`. Shader graphs that sample the material normal still receive
RGB (`encodeMaterialNormalTexel`).

Mips keep the averaged, shortened normal (`rhi::BuildNormalMipmaps`); rebuilding Z from a shortened XY gives a
slightly flatter normal at distance than RGB did, which is the usual BC5 trade-off.

## Extending

The pieces are independent (`TextureCompression.h`):

- `TextureSemantic` names what texels mean; `SelectCompressedFormat` picks the first format, in order of preference,
  that the device and a registered encoder both support.
- `TextureBlockEncoder` encodes 4x4 blocks of one format. `RegisterTextureBlockEncoder` adds or replaces one.
  The built-in encoders use `stb_dxt` (BC1, BC3, BC4, BC5). A BC7 encoder (for example bc7enc) only needs
  registering: the policy already prefers BC7 for textures with alpha, and its `Identity` keeps cache entries apart.
- `BuildTextureMipChain` and `EncodeTexture` are usable on their own; `CookCompressedTexture` (`TextureCooking.h`)
  encodes a texture into a store as a renderer would ask for it.

The RHI accepts complete mip chains for any format (`TextureDescriptor::mipmapsProvided`); compressed formats require
it. `rhi/FormatInfo.h` has the block and level size helpers both backends share.

## Not done

- Only textures named by material files are cooked; textures sampled through other paths encode on first use.
- Terrain layer arrays and impostor atlases stay RGBA8 (both need texture-array or alpha-depth handling).
- No mip streaming: every mip of every resident texture stays in GPU memory
  ([assessment](dev/texture-streaming-assessment.md)).
- Alpha-tested foliage mips do not preserve alpha coverage (true before compression too).

## Checks

- `PlutoGETextureCompressionTests`: format traits, format policy (including a registered BC7 encoder), sRGB mip
  averaging, encode error against the engine's DDS decoder, odd sizes, the store (round trip, damaged entries) and the
  queue (results equal synchronous encodes, invalid jobs fail), and a cooked layer the renderer's lookup hits without
  encoding or writing its cache.
- `PlutoGETextureResidencyTests`: the shared decoder, prefetched loads, releasing and reading back pixels (including
  readers on another thread during releases), material slot sampling, and cooking a project's materials (each sampling
  once, small textures left RGBA8, a second cook reusing the store).
- `PlutoGEVulkanCompressedTextureTests`, `PlutoGEOpenGLCompressedTextureTests` (`--compressed-textures`): BC1 sRGB
  albedo samples like its RGBA8 source; a tilted BC5 normal map shades like its RGBA8 source and differs when
  decoded as RGB; truncated chains are rejected.
