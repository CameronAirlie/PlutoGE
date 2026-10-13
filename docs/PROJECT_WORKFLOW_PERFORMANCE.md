# Project workflow performance

Ordinary saves preserve the active asset context, import watcher and resource caches.
They serialize the active scene, update only that scene's registry entry, and save
the manifest. Unchanged scene bytes retain their on-disk timestamp. The comparison
uses the current serialized scene and actual file contents, so external edits and
deletions are repaired even when timestamps or dirty flags are unchanged. Model
snapshot retention/validation still happens before the comparison.

Editor catalog refreshes request identities and storage rather than content hashes
and dependency scans. Imported payload digest verification remains enabled. Full
validation and cooking continue collecting dependencies and enforcing their existing
integrity checks. Asset creation/import and explicit refresh paths remain responsible
for broader catalog reconciliation.

Exports prefer adjacent installed hosts and the matching CMake runtime/configuration
layout before the existing recursive discovery fallback. Content packs reuse existing
compressed blocks only after CRC filtering, successful decoding and byte-for-byte
comparison with the current source. Invalid old indexes/payloads fall back to encoding.
New packs still undergo full verification and atomic replacement. Deleted files are
excluded by enumerating the current staging directory. Compression changes are
respected. This saves compression work, but cooking/copying and full verification
still run; it is not a timestamp-based cook cache.

## Measuring

The editor console emits `[Project timing]` stage and total milliseconds for opening,
saving, script compilation and exports, including totals on failed operations. Compare
cold/warm opens, unchanged saves, scene edits and repeated exports using the same
project, configuration and drive. Record several samples before comparing medians.

`PlutoGEContentPackTests --benchmark` measures complete pack writing, verification
and publication on 32 MiB of compressible synthetic content with/without block reuse.
This isolates packaging; its speedup is not a whole-project build estimate.
`PackWriteStatistics` exposes source bytes, reused/encoded compressed blocks and
successful operation elapsed time to other benchmark callers.

Run `tools/Measure-EngineBuild.ps1` to record build timings and per-sample logs in the
chosen configured build directory. The first sample builds pending changes; subsequent
samples measure no-change overhead. After a representative source or header edit,
run with `-Samples 1` to measure that incremental build. Adjust `-Jobs` to compare CPU
utilization and memory pressure. The tool does not clean build outputs or edit sources.

PCH, MSVC compile scheduling and incremental ScriptCore/resource dependencies already
exist. Runtime shader staging uses `copy_directory_if_different` on CMake 3.26+ and
retains compatibility with older CMake. Avoid enabling unity builds or increasing
parallelism globally without measuring representative changes and peak memory.

Persistent dependency/cook caches and skipping entire script builds require a separate
invalidation design covering external edits, SDK/settings changes, dependencies,
output deletion and in-flight imports. They are not enabled by these changes.
