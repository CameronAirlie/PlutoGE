# Content packs

## Implementation plan and scope

1. Add a reusable content library independent of the renderer: indexed archive
   reads, bounded format parsing and legacy compatibility.
2. Add independent block compression, integrity verification and safe replacement
   of completed archives. Reuse the engine's pinned zlib dependency.
3. Route runtime project, mesh, animation, material, scene, prefab, texture, audio,
   terrain and UI reads through the content layer. Keep loose editor reads and
   explicit disk materialization for .NET.
4. Expose optional dependency pruning and explicit inclusion roots; preserve stable
   asset identities independently of editor sidecar files.
5. Support ordered additional packs and headless pack creation/verification.
6. Validate random access, corruption rejection, compatibility, overlays, cooking
   and runtime integration. Build the runtime and run relevant existing tests.

These steps implement the pack-system changes. Target-specific texture/audio
conversion remains separate work: the cooker currently copies the existing
representations. Adding such conversion requires format/version decisions,
import settings, target capabilities and matching GPU/audio upload paths. This
change does not claim to introduce GPU-native texture cooking.

## Runtime behavior

`Game.plutopack` is mounted at a virtual project root. Project asset references
still resolve to paths below that root, but `content::InputFile` and texture
memory decoding read from the archive. No full content extraction is necessary.
Loose files continue to use file streams. Reads within a mounted namespace never
fall back to arbitrary loose files when an entry is missing or damaged.

The low-level `Pack::Read` API accepts byte ranges and decompresses only the blocks
intersecting a read. The stream adapter currently holds one requested packed asset
in memory; it is not an asynchronous streaming scheduler. Large texture/mesh
streaming can use range reads later. CRC checks occur when blocks are accessed;
`--verify-pack` scans all payloads without launching graphics.

Managed DLLs, PDBs, `.deps.json` and `.runtimeconfig.json` files are materialized
under the virtual root for hostfxr. Game assets read through custom C# System.IO
calls are not automatically virtualized. Such code must use engine asset APIs or
an explicit native materialization integration. A crash can leave the temporary
managed-file directory behind; normal shutdown removes it.

`PlutoAssetIds.manifest` stores the ID/reference map, including identities of
source models excluded from shipping. `PlutoCook.manifest` describes included
files. Runtime model resolution no longer depends on shipping `.plutometa` files.

## Format version 2

All integer fields are unsigned little-endian. No XOR obfuscation or encryption.
Header (40 bytes):

| Field | Bytes |
|---|---:|
| `PLUTOPK2` | 8 |
| Version = 2 | 4 |
| Block size = 262144 | 4 |
| Index offset | 8 |
| Index byte size | 8 |
| Index CRC32 | 4 |
| File count | 4 |

File payload blocks follow the header. The index occupies the end of the archive.
Each file entry contains a 32-bit UTF-8 path length, path bytes, 64-bit decoded
size, 32-bit block count, then block records. A block record contains a 64-bit
absolute offset, 32-bit stored size, decoded size, decoded CRC32 and codec.
Codec 0 is raw; codec 1 is a zlib stream. Compression uses zlib's fastest level and
falls back to raw when compression does not reduce the block size.

Paths must be relative, normalized and free of traversal, drive prefixes,
Windows device names and ambiguous separators. Duplicate paths, file/directory
collisions, unsupported codecs, overlapping/out-of-bounds blocks, truncated
indices and invalid checksums are rejected. Paths are limited to 4096 bytes and
the index to 256 MiB. CRCs detect accidental damage, not malicious modification.

Writers sort entries, write to an exclusively reserved sibling temporary
directory, verify the result and atomically replace the destination pack. A pack
write failure leaves the previous pack intact. The complete multi-file game export
is not transactional: executable/dependency copies are separate operations.

Legacy `PLUTOPK1` packs are indexed by walking entry headers and seeking past
payloads, without extracting them. Their fixed XOR encoding is decoded on read;
they have no integrity checks, so verification can only validate their structure
and readability. A new runtime is required to read version 2 output.
Runtime binaries advertise their supported format through an embedded capability
marker (`--pack-version` prints it). Editor discovery skips binaries without the
current marker. Export checks it before changing any output files and reports
the stale runtime path when it is incompatible. This prevents a newly built
editor from pairing a version 2 pack with an older runtime executable.

## Inclusion controls

`ExportOptions` and `Export-Game.ps1` support compression, pruning and explicit
`project://` inclusion roots. Default inclusion stays broad for compatibility.
Pruned export walks dependencies from the startup scene, script assembly and
explicit roots. Missing project dependencies or incomplete scans fail the cook.
Native/managed assembly companion files beside the configured assembly are kept.
A static scanner cannot discover arbitrary runtime-generated filenames: list
those assets or containing scene/prefab roots explicitly.

## Additional packs and patches

Create a directory containing changed/new runtime-ready assets at their original
relative paths, for example `Assets/Materials/Stone.plutomaterial`, then run:

```powershell
.\PlutoGERuntime.exe --pack C:\PatchContent C:\Game\patch-001.plutopack
.\PlutoGERuntime.exe --verify-pack C:\Game\patch-001.plutopack
```

Add `Game.plutomounts` beside `Game.exe`, one sibling `.plutopack` filename per
line. Blank lines and lines beginning with `#` are ignored. Packs are mounted in
listed order after `Game.plutopack`; later entries override earlier entries with
the same asset path. A missing/invalid listed pack fails startup. Unlisted packs
are not discovered automatically. Keep packs immutable while the game runs.

This is replacement/addition layering, not a patch generator or deletion system.
Include updated project/identity manifests when introducing assets that need
registry lookup or new stable IDs. Group and distribute these files consistently.
Already-loaded assets are not refreshed by mounting a pack; mount before loading.
Compression formats and logical content groups remain independent.

## Validation

`PlutoGEContentPackTests` covers compressed/raw round trips, cross-block range
reads, stream seeks, empty files, legacy input, damaged indices/payloads, unsafe
paths, overlay precedence, loose-file isolation, selective materialization and
failed/repeated writes. `PlutoGEAssetReferenceCookingTests` covers pruned dependency
closure, explicit roots, missing roots, mounted project lookup and stable IDs.

Local verification on Windows (RelWithDebInfo): the editor and runtime built;
pack/cooking, asset-reference, model, UI, large-mesh, prefab-variant and
surface-response tests passed. Large-mesh and prefab tests required execution
outside the sandbox for temporary-directory resolution.
A copy of the existing ShadowSouls build completed a packaged smoke run, including
scene/material loading, managed scripts, RmlUi markup/styles and font loading,
and normal temporary-directory cleanup. The smoke build used its configured
upscaler fallback; this was a content-loading check, not a rendering benchmark.
Repacking the same 23 legacy files reduced archive size from 739,512 to 252,234
bytes (65.9%). This is one sample, not a general compression or startup-time claim.
