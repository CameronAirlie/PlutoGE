# Load times

How the editor and the standalone runtime get from launch to an interactive first frame, what made that slow, and how
to measure it. Figures are SurvivalGame (LakeDistrict) on a Ryzen 5700G with Vega 8 graphics, 2026-11-24.

| Session | Before | After |
| --- | --- | --- |
| Editor opening the project (to its third frame) | 12.6 s | 5.8 s |
| Runtime from the source project (to its second frame) | 14.5 s | 4.6 s |
| Exported game, first launch on a machine (cold driver shader cache) | 57.4 s | 21.8 s |
| Exported game, later launches | 5.1 s | 4.1-4.5 s |
| Exported game, launched from a new folder or after the driver dropped its cache | 57.4 s | 6.1 s |

## Measuring

`PLUTO_LOAD_PROFILE=<file>` turns on `core::LoadProfile` (`LoadProfile.h`) in both hosts. It writes:

- **Milestones**, as wall time since the process started: engine initialised, scripts loaded, startup scene loaded and
  the first frames (runtime); editor initialisation, project open, panels and the first three frames (editor).
- **Main-thread scopes** recorded inside load phases (`LoadProfile::Phase`), summed by name with self time, calls and
  the longest call. Any `core::CpuScope` on the main thread appears, so new load work is measured by adding scopes;
  code below the hosts never depends on `LoadProfile` itself.

The runtime writes after its second frame and at exit; the editor after its third frame. To reproduce a player's
first launch, run the exported game from a folder it has not run from (drivers key their shader cache by path) with
`PLUTO_PIPELINE_CACHE=0`.

## What was slow, and what changed

### Asset catalog scan (editor and source runtime: about 6 s)

Opening a source project scans its assets (`AssetDatabase::Scan`). Nearly all of it was SHA-256 of every imported
Library file (1.9 GB for SurvivalGame) on one thread, every launch, because the checks of verified files lived only in
memory.

- `ImportedFileCheckCache` persists its checks to `Library/ImportedFileChecks.tsv` (`Load`/`Save`; a file is trusted
  while its size and modification time are unchanged, as within a session). `AssetScanOptions::importedFileCheckPersistence`
  chooses: the editor reads and writes; the source runtime only reads (its startup stays read-only). Cooking and
  validation still hash everything.
- Files not yet trusted are hashed side by side (`ImportedFileCheckCache::VerifyAll`), as are the storage locations
  and the asset metadata sidecars.
- `Project::RefreshAssetRegistry` names files lexically under the asset directory instead of canonicalising each.

6.0-6.9 s became 0.4 s warm (1.1 s cold).

### Pipeline compilation (exported games: about 50 s on first launch)

Vulkan pipelines compiled one after another while `RhiRenderService::Initialize` held the device lock, and the engine
had no pipeline cache of its own, so it relied on the driver's (kept per executable path). Two uber-shaders take
15 s and 12 s to compile on this driver.

- `VulkanPipelineCompiler` (`rhi/vulkan/VulkanPipelines.h`) compiles on workers. Creating a pipeline validates and
  builds its layouts at once and returns; the first bind waits for it, or compiles it on the binding thread if no worker
  has started it, so a small pipeline needed now never queues behind a large one. Scene loading overlaps the compiles.
  `PLUTO_PIPELINE_COMPILE_THREADS` sets the worker count (0 compiles serially at creation, as before, failures included).
- `VulkanPipelineCacheFile` keeps a `VkPipelineCache` per executable under the user's local cache directory
  (`%LOCALAPPDATA%/PlutoGE/PipelineCache`), checked against the device and driver, saved whenever the compile queue
  drains and at exit. `PLUTO_PIPELINE_CACHE=0` disables it.

A first launch still waits for the longest single compile (the opaque uber-shader, about 15 s here). Shortening that
means changing the shader itself.

### Texture decoding (every session: 1.4-3.5 s)

Material textures decoded on the main thread, one material at a time, even in exported games whose cooked compressed
copies were already in the pack (finding them needed the decoded texels). The
[source index](TEXTURE_COMPRESSION.md#source-index) now loads indexed files without decoding them; terrain layers and
impostor atlases, which stay RGBA8, start their decodes together.

## Remaining

- The first UI paint rasterises every glyph as a signed distance field with FreeType (about 0.45 s): a persisted
  glyph atlas would remove it.
- SurvivalGame's ambience scripts decode their clips on the first frame (about 0.6 s); preloading them in the game, or
  an engine API that decodes several clips side by side, would hide it.
- Animation component deserialisation (about 0.26 s) has not been examined.
