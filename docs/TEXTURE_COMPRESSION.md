# Texture compression

Material textures are sampled from block-compressed copies (BC1, BC3 and BC5) instead of RGBA8. A compressed texture
takes a quarter (BC1) or half (BC3, BC5) of the memory, and every sample reads correspondingly fewer bytes. On an
integrated GPU, which shares system memory bandwidth with the CPU, that bandwidth is the main win.

Source images are unchanged. The engine still decodes them to RGBA8 on the CPU (CPU users such as the shader-graph
preview, terrain painting and thumbnails keep reading those pixels), and the renderer encodes what it uploads.

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
- `BuildTextureMipChain` and `EncodeTexture` are usable on their own, for example to cook compressed textures into
  content packs instead of encoding them on first use.

The RHI accepts complete mip chains for any format (`TextureDescriptor::mipmapsProvided`); compressed formats require
it. `rhi/FormatInfo.h` has the block and level size helpers both backends share.

## Not done

- Textures are encoded on the user's machine on first use, not cooked into content packs.
- Terrain layer arrays and impostor atlases stay RGBA8 (both need texture-array or alpha-depth handling).
- No mip streaming: every mip of every resident texture stays in memory.
- Alpha-tested foliage mips do not preserve alpha coverage (true before compression too).

## Checks

- `PlutoGETextureCompressionTests`: format traits, format policy (including a registered BC7 encoder), sRGB mip
  averaging, encode error against the engine's DDS decoder, odd sizes, the store (round trip, damaged entries) and the
  queue (results equal synchronous encodes, invalid jobs fail).
- `PlutoGEVulkanCompressedTextureTests`, `PlutoGEOpenGLCompressedTextureTests` (`--compressed-textures`): BC1 sRGB
  albedo samples like its RGBA8 source; a tilted BC5 normal map shades like its RGBA8 source and differs when
  decoded as RGB; truncated chains are rejected.
