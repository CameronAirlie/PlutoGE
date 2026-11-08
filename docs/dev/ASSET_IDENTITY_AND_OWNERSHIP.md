# Asset ownership and identity contract

Date: 8 October 2026
Status: Initial migration contract; implementation status is listed separately below.

## Architectural boundaries

Project owns project configuration and path presentation. AssetDatabase owns the editor inventory and indexed identity lookup. Import services will own conversion from source/settings into generated object descriptors. AssetManager owns loaded resources and will consume catalogs rather than discover identities itself. Cooking owns the runtime dependency closure and runtime catalog. The content browser presents these services and must not become their implementation owner.

Persistent identity, physical storage, and resource lifetime are distinct concerns. Moving output into Library must not change object identity. Replacing a database snapshot does not replace live mesh pointers; publishing a new import generation must account for existing resource users separately.

## Ownership inventory

| Existing type | Default ownership | Future import treatment |
| --- | --- | --- |
| Model | Source | Owns imported sub-objects and generated model prefab |
| Texture, Audio | Source | Source bytes retained in Assets; target representations may live in Library |
| Script | Source | Compilation outputs handled separately from model import |
| Assembly | Source/build input | Preserve existing assembly companion handling until compilation ownership is specified |
| Mesh | Authored or imported | Use explicit provenance; never infer ownership solely from extension |
| Animation, AnimationClip | Authored or imported | Imported clips are regenerateable; edited/extracted clips become authored |
| Material | Authored or imported | Source-wide remaps and instance overrides have different scopes |
| Scene, Prefab | Authored | Generated model prefab is a distinct importer-owned object |
| ShaderGraph, AnimationGraph | Authored | Referenced inputs and runtime dependencies remain distinct |
| ParticleSystem, PostProcessPreset, ScriptableObject | Authored | Must participate in logical reference migration and cooking |
| RmlDocument, InputMapping, SurfaceResponse, LoadingScreen | Authored | Preserve external resource dependencies and current runtime behaviour |
| Unknown | Unclassified | Preserve content; require a registered policy before automatic cleanup |
| Count | Sentinel | Never an asset |

A .plutomodel manifest is importer output. Existing generated native files are not disposable until migration proves provenance and preserves any edits. No cleanup uses this table or a filename extension alone as evidence that deletion is safe.

## Identity policy

Keep existing sidecar ID strings unchanged during the foundational phase. IDs are opaque values; this phase introduces no new GUID format or scene serialization. Source paths remain supported compatibility references. Future project sub-object references will pair the existing owner ID with a persistent local object ID; local ID zero is reserved for the main object. Engine built-ins keep their engine:// namespace until a unified resolver explicitly handles them.

The current type/name hash for model sub-objects remains a legacy algorithm. It must not become the long-term persistence guarantee: duplicate names and renames require saved correspondence and explicit ambiguity handling. New artifact keys must never become gameplay identities.

A source move carries its metadata. A deliberate duplicate creates a new identity through an explicit editor operation. An unexpected duplicate is a conflict, not permission to silently rewrite either file. Missing metadata may receive a new ID under current discovery rules; existing unreadable or unsupported metadata is preserved and reported, because replacing it could destroy identity or future settings.

## Snapshot and API contract

AssetDatabase::FindById and FindByReference return borrowed pointers into the same published snapshot. Missing keys return null. Successful Scan replaces the snapshot and invalidates those pointers. Failed Scan retains the previous records and indexes, including pointer validity. Consumers must not hold records across a successful rescan without copying their data or reacquiring by identity.

The scan is not a complete filesystem transaction: it still refreshes the Project registry and may create metadata for newly discovered files before a later failure. Retaining the database snapshot guarantees coherent indexed reads; it does not imply rollback of discovery side effects. A later metadata/import transaction service will address atomic writes and publication.

Duplicate-ID diagnostics identify the ID and both conflicting references. Scan never chooses a winner or edits duplicate metadata. Recovery requires resolving the metadata conflict explicitly and rescanning. Metadata read errors similarly preserve the original bytes.

## Compatibility and format evolution

This first slice keeps PLUTOASSET version 1, current project references, existing artifact paths, native encodings, and content pack formats. No project conversion occurs automatically. It tightens behaviour for invalid or duplicate metadata: scans now fail with diagnostics instead of silently changing identity.

Before adding settings to metadata, introduce a dedicated reader/writer with supported-version checks, unknown-field preservation, explicit parse errors, and atomic publication. Existing newer or malformed files must not be treated as absent. A project format gate must precede writing a format that older editors could damage. Establish structured logical-reference serialization before changing scene/prefab writers; preserve legacy reads during adoption.

Do not add ownership flags to current records based solely on file type. Populate authoritative provenance from import manifests or explicit authored creation, then migrate legacy assets conservatively. Import services and runtime catalogs should depend on shared value types and descriptors, not editor UI classes or graphics initialization.

## Implemented foundation

- Public indexed lookup of existing asset IDs.
- Scan publication through a locally built replacement snapshot.
- Duplicate identity rejection without metadata rewriting.
- Preservation and rejection of existing unreadable/unsupported metadata.
- Regression checks for ID/path lookup agreement, failed-scan pointer stability, conflict diagnostics, metadata byte preservation, recovery, and rename-with-sidecar identity preservation.

## Outstanding work

The full ownership audit of serializers and managed fields, logical reference type/schema, persistent model correspondence, metadata transactions, importer extraction, Library cache, UI adoption, generated hierarchy, runtime catalog, and migration remain subsequent work. This contract does not claim milestones 1 or 2 are complete. Refer to asset-management-transition-plan-2026-10-07.md for the full sequence.

## Continued implementation — 8 October 2026

A dedicated AssetMetadata module now owns bounded parsing, explicit missing/invalid/unsupported/IO status, opaque ID generation, unknown-record preservation, and same-volume atomic creation/replacement. Creation is exclusive; replacement requires the same existing identity. This is not concurrent edit merging.

AssetReference supplies a strict asset://owner#local-ID codec with percent-encoded owner IDs. AssetCatalog is an IO-free typed descriptor index shared through retained const snapshots. Database scans publish catalogs of main assets and manifest sub-objects; shared physical locations may have multiple logical aliases. AssetManager can consume these snapshots while retaining legacy resolution when no catalog is supplied. Serialized scenes have not yet been converted to the new codec.

Model import orchestration has moved from the content browser into a separate PlutoGE::AssetImport module and the PlutoGEImportModel command-line tool. A shared material writer serializes CPU data without shader compilation or Texture proxy objects. Reading mesh material metadata no longer creates a live mesh merely to obtain bindings. Legacy co-located output remains in use during reference migration.

ImportFileTransaction stages a batch, records intent, retains original backups until acceptance, and recovers interrupted publication. Transaction backups live in .pluto-import-transactions outside disposable Library. Publication remains a serialized legacy multi-file operation; generation-based Library publication is still pending. File handles must be closed before Windows journal cleanup. Recovery covers process interruption; power-loss durability is not yet guaranteed by fsync/FlushFileBuffers.

Project format version 2 gates persistent source importer settings and object correspondence. New Project::Create projects use version 2; loaded version 1 projects retain version 1 and are not automatically migrated. Source metadata records MODEL_IMPORT, MODEL_OPTIONS, MODEL_OBJECT, and MODEL_MATERIAL_REMAP store options, stable IDs, retired entries, and authored/engine material remaps. The main metadata schema stays version 1; the enclosing project gate prevents old editors from discarding these semantics.

Current source keys are mesh/main, material/slot/<index>, texture source-relative location plus color space (embedded textures use index), clip/name, and animation/set. They improve source-file rename stability but do not promise identity preservation across material-slot or embedded-texture reordering. Duplicate/ambiguous correspondence is rejected. Removed IDs are retained and not recycled. Source-native identifiers and user-directed repairs remain future work.

The new service bypasses the old source-stamp-only mesh cache and collects external buffers/textures as dependency inputs. This avoids the old cache masking external-file changes. Complete artifact hashing, reverse dependency indexing, cache publication/rebuilding, editor import-settings UI, generated model hierarchies, runtime catalogs, and migration remain pending.

### Cache and runtime catalog groundwork — 8 October 2026

Added portable streaming SHA-256 content digests and immutable artifact generations with versioned manifests, target/importer/settings/input keys, output integrity checks, isolated staging, corrupt-generation quarantine, and deterministic-producer checks. The model service stores generations under Library/Artifacts while retaining legacy physical outputs for compatibility. Cache generations are currently an additional validated copy: warm imports still parse and generate, and native artifacts have not moved out of Assets. Reverse dependency indexing, latest-generation selection, warm reuse, garbage collection, and relocation remain pending.

ModelSourceSnapshot discovers external input paths, hashes their contents before and after the authoritative parse, checks that the dependency set is unchanged, and rechecks inputs before publication. Discovery data is released before the authoritative parse to avoid retaining two full model payloads. This uses two parses until importer IO adapters can capture dependencies in one pass. An edit followed by an identical-content restoration between checks is outside this snapshot guarantee. The service bypasses the legacy source-stamp cache. Tests cover URI-encoded external glTF buffers and edits injected before cache publication.

Added versioned catalog serialization with bounded, transactional parsing; bounded AssetManager catalog loading; logical URI path/mesh/texture resolution; and preservation of logical URI strings by path persistence helpers. ClearProjectContext now releases the catalog. Cooking emits a catalog restricted to shipped objects, and runtime startup loads it when present. Old packs without a catalog retain their compatibility path. Scene/prefab writers and managed asset fields have not been converted, so this is runtime catalog groundwork rather than identity-only runtime completion.

Regression coverage now includes known SHA-256 vectors, input invalidation, corrupt-cache rebuilding, nondeterministic producer rejection, Library deletion/rebuild without identity or remap changes, failed catalog load preserving its prior snapshot, malformed/duplicate recovery journals, and packed object resolution without source/model-manifest files. The eight focused tests passed before the newest catalog integration; its final rebuild and verification are in progress.

### Additional validation and concurrency — 8 October 2026

Logical references are now recognized by text and length-prefixed binary dependency scanning. Database scans resolve them through the catalog for dependency closure and record missing identities as scan errors; pruned cooking rejects reachable unresolved identities. Explicit cook roots can also use owner/local-ID references. Project validation accepts an optional catalog and reports logical references as unverified when none is available.

Project imports and cache publication now hold OS-level locks, preventing separate editor/CLI processes from recovering active transactions or racing cache replacements. Lock files persist, while operating-system ownership ends automatically on process exit. Project metadata and existing material-override files participate in input snapshots; concurrent changes are preserved and cause import rejection. Newly appearing override files are rejected rather than overwritten. This remains a before-publication snapshot check rather than a general-purpose filesystem transaction for arbitrary external writers.

A read-only inspection of CoD found 12 model manifests with no duplicate local IDs or source-sidecar identity mismatches. Its 1,129 metadata files include one duplicate ID on an orphan sidecar for SourceModels/grass_green/scene.gltf; that source file is absent. No project files were changed and the orphan was preserved. This is a narrow identity audit, not completed migration or visual validation.

The RelWithDebInfo editor/runtime/CLI build and ten focused CTest cases passed after catalog, dependency, and locking integration. Subsequent logical explicit-root coverage also passed. CPU texture writing was extracted for reuse and gray/alpha preservation; its newest regression is under verification. Library cache reuse and relocation, complete serializer/managed adoption, editor settings/extraction, model hierarchy, and final migration remain outstanding.

### Warm reuse and live publication — 8 October 2026

Unchanged model imports can now restore a validated immutable generation without parsing the source. A shared ModelArtifactSettings fingerprint includes source metadata, options, logical source location, effective material bindings, and output layout. Restoration verifies current external inputs, source metadata/override snapshots, artifact hashes, generated provenance, and override-file presence, then publishes through the same recovery transaction. Material ID remaps whose resolved location changes take the normal importer path. Failure leaves the caller's previous result and published catalog intact.

Cache generation lookup is deterministic and currently scans generation manifests. It filters requests before hashing matching output payloads, but still needs a persistent request/dependency index and measured large-project performance work. Corrupt/unavailable cache entries fall back to normal importing. Import recipe/version/target constants are centralized; output-affecting importer dependency or algorithm changes require a version bump.

Warm regression coverage passed: unchanged imports report cache reuse and skip the parse stage; changed external glTF buffers regenerate different mesh geometry; corrupt cached payloads are excluded; metadata/dependency edits during restoration reject publication; source rename and deleted Library still rebuild with stable source-owned IDs/remaps.

Committed imports now report changed asset locations. The editor notifies its resource manager after successful publication, evicting stale future mesh/texture lookups and CPU metadata while retaining existing borrowed mesh/texture objects. Cached materials reload in place. Existing scene meshes do not automatically switch to a new generation; full live instance reconciliation remains future work. Targeted native-mesh borrower and cached binding notification coverage is under verification.

## Logical writer adoption — 8 October 2026

AssetManager has an explicit per-type logical serialization policy, reset with project context. Version-2 editor projects currently enable native Mesh and Material references after a successful catalog scan. Legacy projects preserve path output. Unknown, missing or ambiguous catalog locations retain their path representation; existing logical URIs are preserved. Shared physical outputs prefer a unique source-owned subobject, then a unique standalone main identity.

SceneSerializer recognizes MeshAssetReference and the legacy SourceMeshPath/SourceMesh aliases. Scene round-trip coverage proves a saved native mesh URI survives physical rename when the new catalog retains its identity. Metadata and binding lookup resolve logical references to the current physical location before caching. Content-browser refresh and asset mutation paths publish validated catalog snapshots through EditorShell.RefreshProjectAssets. Failed scans preserve the previous catalog and report errors; external edits still require explicit refresh. This synchronous scan is an interim boundary before incremental indexing.

Runtime executables advertise both the content-pack contract and asset pipeline version 2. Export checks both before creating destination files. Legacy projects accept the existing content-pack marker; version-2 projects require the new marker. Runtime discovery still chooses by the existing content-pack contract and export rejects incompatible candidates.

RelWithDebInfo editor/runtime and all thirteen targeted asset regression cases passed for this writer-adoption batch. Full managed-field adoption and other asset writer types remain pending.

## Background imports and shared project ownership — 8 October 2026

ModelImportTask owns copied project/request values and a joined worker thread. It exposes synchronized progress and one consumable completion; a new start cannot replace running work or an unconsumed result. Cancellation is checked before and after progress callbacks. Parsing itself observes cancellation at service stage boundaries, so stopping a large parser is not immediate. Owner destruction requests cancellation and joins before worker-visible state is released.

The editor polls completions in its frame loop, publishes catalogs and refreshes GPU resource lookups on that thread, and keeps drafts separate from committed source settings. The source details panel exposes progress and Cancel for imports/reimports. Project context changes request cancellation and completions for another project are discarded without publishing their resources. Refresh avoids scanning an in-process active publication. External source-copy/import entry points still use their synchronous adapter and reject concurrent editor imports.

ProjectAssetLock now lives in Assets, allowing both the importer and cooker to hold the same OS lock; ProjectImportLock remains a compatibility alias. The existing lock filename is retained. Cooking acquires ownership before database scanning or output creation, and a regression proves it fails without destination writes when an import owns the project. Editor export also rejects an active background import. This does not yet make the whole standalone-export publication transactional.

Background lifecycle/cancellation/snapshot tests passed, as did the editor build and thirteen asset regressions. Shared lock integration then passed cooking, transaction, cache and importer tests. Catalog ownership now hides generated native objects from ordinary browser folders while retaining model subasset access. Texture inputs remain visible until persisted output provenance distinguishes them from generated textures.

### Authored extraction and direct-save ownership — 8 October 2026

Metadata optionally records OWNERSHIP as AUTHORED or SOURCE. Legacy missing ownership remains unclassified; imported ownership is derived from model manifests and cannot be asserted by a sidecar. Unknown metadata records remain preserved. A newly extracted native object receives a new authored main identity. EXTRACTED_FROM is historical provenance and must never be used for authoritative source-object resolution.

ModelObjectExtractionService is CPU-only and shares the project lock and recoverable file transaction with import. Mesh extraction clears source model linkage while retaining geometry, skeleton, animation nodes, import options, and effective bindings. Other native copies retain shared dependencies. Material extract-and-use persists authored IDs in version 2 source settings and publishes those settings, mesh binding overrides, and the new asset together. Legacy version 1 retains path binding overrides without upgrading its format. Extract All currently commits each object independently.

AssetManager direct saves reject imported mesh, material, clip, and animation-set locations when a catalog is supplied, including logical and physical aliases. Controlled importer writers use a private manager. Editor catalogs now apply to legacy projects too; only version 2 enables logical writing of the supported mesh/material fields. MeshEditor no longer synchronizes authored mesh edits back into an imported canonical object. Existing extracted meshes with retained legacy provenance are not automatically rewritten; ambiguous legacy relationships require a separate repair workflow.

The engine/editor build and all 16 asset pipeline regressions passed for independent mesh extraction and explicit ownership. Generalized material extraction additionally passed its CPU integration test, including cold reimport after deleting the test project's Library. The final editor adapter build succeeded. Cross-process refresh locking and exception-to-diagnostic handling are being verified next.

### Accepted state, reverse dependencies, and provenance — 8 October 2026

Successful imports optionally persist accepted input hashes and a generation key in Library/ImportState. This state is disposable and contains no authoritative object mapping. Cold and warm publication recheck non-authored dependencies before acceptance; canonical source settings and binding overrides record their accepted hashes. ImportDependencyIndex reconstructs reverse import edges from that state. Runtime-only references do not become geometry import dependencies. Missing state selects its owner conservatively.

The --check CLI mode is read-only and does not parse models or create source sidecars. It distinguishes current sources, required imports, and blocked conflicts. --affected queries import owners for an absolute changed input path. These primitives are available without enabling automatic watching or background startup imports yet.

Version 2 model packages now retain generated native-file SHA-256 baselines outside Library. Reimport protects modified generated bytes, even when force bypasses cache reuse. An extracted/remapped material records the exact preserved source-content hash with its independent authored identity. Package baselines are transitional: moving manifests to Library requires persisting equivalent proof with source metadata first. Legacy content without a baseline still needs an explicit migration audit rather than an assumption that generated bytes are disposable.

Model manifest parsing is bounded, transactional, strict for known records, and preserves unknown records. Legacy minimal/source-inferred manifests remain compatible. Legacy basename fallback now verifies source ownership before resolving a package. All 16 asset regressions and the engine/editor build passed after this slice; the real CoD reconciliation remained read-only.

### Editor publication and live bindings — 2026-10-08

Version 2 startup reconciliation is read-only until a verified source is queued for import. New source packages and existing packages with persistent generated-file baselines can rebuild automatically; unverified legacy output requires manual review. Completion carries the project path and a context epoch. Resource and scene publication runs on the editor thread, and model processing and Play are mutually exclusive at editor entry points.

Scene::ApplyModelAssetGeneration resolves imported mesh owner/local identities against the newly published catalog. The previous snapshot records both binding references and borrowed default material pointers. This distinction preserves explicit in-memory material instances while inherited defaults adopt the new source binding. Removed objects retain their last valid borrowed mesh and emit diagnostics. Animation pose/binding caches are invalidated; clip-list and topology-aware override reconciliation are not yet implemented.

Asset moves retain file identity but directory moves do not yet provide embedded-reference rewriting or crash recovery. Generated model manifests and native outputs still reside in Assets; Library-only relocation must first preserve source correspondence and generated-file provenance in persistent source metadata.

### Persistent source package snapshots — 2026-10-08

MODEL_PACKAGE version 1 and MODEL_PACKAGE_RECORD records now carry the typed model package snapshot in the source sidecar. The existing model codec validates its records; source ownership and project locations receive additional validation. Source metadata is authoritative when this snapshot exists, while legacy native manifests remain a fallback. Unknown metadata/package records survive rewriting. The 1 MiB metadata limit remains enforced.

Version 2 import publishes snapshot, correspondence, settings, and generated output in one recoverable transaction. Recipe version 4 distinguishes the resulting metadata bytes. Catalog, model browsing/placement, reconciliation, and reimport can retain object identity and output baselines after native manifest and Library deletion. Native output storage still remains in Assets; this change establishes persistent provenance before relocation.

### Watching and moves — 2026-10-08

Idle version 2 editors now capture cheap import-input snapshots on workers and debounce changes before authoritative content reconciliation. Watching does not publish assets, repair metadata, or decide overwrite safety. Reconciliation and transactional import retain those responsibilities. Snapshot detection uses file type, timestamp, and size; an external edit that deliberately preserves those hints needs explicit refresh/check/reimport. Context epochs, cancellation, and edit-mode scheduling prevent publication into stale or playing contexts.

Independent generated-object/manifest moves are rejected. Moving a directory containing an imported package is also rejected until embedded-reference migration is supported. Generic authored moves and standalone source-file moves retain the previous identity-preserving behavior. Project infrastructure is excluded even for assetDirectory='.', with canonical Windows root aliases and case-aware comparisons.

### Generated dependency references — 8 October 2026

Version 2 model-generated materials, meshes, and animation sets use logical references for generated textures, materials, and clips respectively. Catalog descriptors retain current physical locations. Authored material remaps use main-object identities; another imported subobject must first be extracted. Recipe version 6 separates these native bytes from older generations. Version 1 remains compatible with physical bindings. Read-only AssetDatabase scans can omit metadata creation, content hashing, and dependency discovery independently; they never invent identities for missing sidecars.

### Migration diagnostics and final publication checks — 8 October 2026

Read-only identity audit includes orphan sidecars in duplicate detection and never creates missing metadata. Reference migration dry runs report possible occurrence mappings with file hashes and unresolved diagnostics; conflicting metadata blocks mapping. These reports are advisory and cannot be applied as textual replacements. Conversion needs actual serializers, backups, locked revalidation, and rollback. Final cold/warm import acceptance now verifies immutable generation inputs and published output digests after catalog validation and callbacks.

Editor event scheduling uses reverse input ownership only when every changed path has coverage. Unindexed changes use full reconciliation, preserving checks for modified native outputs and missing cache generations. Explicit refresh and startup always perform full checks.

### Library storage boundary — 8 October 2026

AssetStorageMap is an immutable virtual-location to physical-input mapping with expected SHA-256 digests. Database scans validate imported ownership, canonical project Library containment, ordinary files, and hashes, then publish normalized storage alongside the catalog. Generated records can exist without native files or invented native-file IDs in Assets. AssetManager resolves and persists through the mapping; cooking reads Library files but writes ordinary relative runtime locations. Default active-generation publication is not switched yet. CPU loading, cooking, corruption rejection, containment/ownership rejection, and packed-only runtime resolution passed in a disposable textured model fixture.

Generated native mesh source metadata is import provenance rather than a runtime dependency. Current version 4/5 trailers retain that distinction in reference scans; future formats remain conservative. Direct source-model runtime references still retain their buffer dependencies. Relative glTF URI escapes are decoded for reference discovery, with malformed/control-byte/outside-root paths reported.

### Active Library generations — 8 October 2026

New projects use pipeline version 3. Imported native products stay in immutable Library generations; source metadata and authored material overrides publish through the existing recoverable transaction. Version 1/2 projects keep their native Assets layout. MODEL_ARTIFACT version 1 selects a generation in source metadata; persistent package hashes/correspondence reconstruct storage without Library import state. Derived pointer bytes are excluded from recipe settings and serialized deterministically. Recipe version 7 separates storage modes.

Recovery scans keep identities/expected routes while disabling unavailable storage. They never authorize stale Assets fallback. Strict cooking verifies cache bytes and emits relative runtime locations. Cold/warm acceptance verifies both the full immutable generation and authored published files. Cache corruption/deletion is rebuildable; source metadata, correspondence and authored remaps remain authoritative. Existing co-located outputs are not automatically removed during a format change.

SetAssetSnapshot publishes catalog and storage together on the owning thread. Unambiguous material identities preserve borrowed pointers across physical generation changes, including temporary unavailable routes; private scene copies remain independent. Shared ambiguous routes retain last-good objects. Mesh override files remain under canonical Assets paths. Runtime compatibility advertises version 3 while preserving version 2 runtime support for version 2 projects. All 19 focused pipeline regressions passed, including Library-only imports/rebuilds, extraction/remaps, final corruption rejection, packed-only geometry, and live material relocation. Garbage collection/leases, generic importers, generated hierarchy instantiation and project conversion remain outstanding.

### Model node correspondence — 8 October 2026

Version 3 imports persist node source keys in model correspondence, sharing its unique allocator and retired tombstones. CPU-only reconciliation accepts optional persistent producer identifiers, otherwise uses unique named parent paths with bounded versioned hashes. Indices are snapshot-local. Reorder/transform changes and distinct-sibling insertion preserve identity; fallback rename/reparent creates a new ID, and restoring a key reactivates its tombstone. Duplicate/anonymous paths, duplicate producer IDs and unresolved ancestors remain explicitly unidentified. No prefab entities or catalog entries are invented. Recipe version 8 covers this change. Generated hierarchy serialization, exact-matrix/vertex-baking compensation, explicit ambiguity mapping and instance override reconciliation are still required. All 19 regressions and an isolated M16 cold/warm/check validation passed.

### Lossless hierarchy input — 8 October 2026

Recipe version 9 adds a private .plutomodelhierarchy Library product. Version 1 binary data preserves exact local matrices, selected roots, node correspondence/unresolved states and baked/animated/skinned binding provenance; world transforms are rebuilt. Source metadata holds only a virtual-location/SHA-256 descriptor. The verified reader checks Library containment, digest, owner, active node keys and mesh identity, leaving decoded output unchanged on failure. Malformed/future descriptor schemas are not rewritten. The private artifact is watched, included in immutable cache validation and safely reconstructed after deletion/corruption. No new ordinary catalog object or runtime dependency is created. All 19 regressions passed, plus isolated M16 cold/warm/check validation. Scene matrix support, generated prefab instantiation and override reconciliation remain outstanding.

### Existing-project output namespaces and source runtime — 8 October 2026

Version 3 source sidecars may select a virtual output directory with
`MODEL_OUTPUT_DIRECTORY\t1\tproject://ImportedModels/<source-id>`. This separates
new imported products from older native files retained as authored assets.
The generated bytes still live in Library. The default output namespace stays
beside the source when this record is absent. Parsing rejects unsupported or
duplicate records, traversal and unsafe directory chains without creating
directories during resolution. Keep this setting with the source sidecar.

The import CLI's read-only `--inspect-source <project://source>` operation lists
the actual parser's object names and source keys for explicit legacy identity
adoption. Import and inspection share texture source-key construction, including
color-space suffixes. Final import catalog validation skips unrelated authored
content/dependency scanning while retaining imported storage validation.

Runtime startup loads the exported catalog when present. A source project with
no exported catalog instead receives a read-only AssetDatabase snapshot,
including its verified Library storage routes. Startup does not generate
metadata or import missing products. Rebuild missing imports before running.
CoD's existing native gameplay assets are deliberately retained as authored
content; changing project version alone is not an automatic conversion of
hand-edited native files into disposable imported objects.

## Affine scene transform prerequisite — 8 October 2026

Entity local transforms can retain a finite linear correction after ordinary XYZ Euler TRS: local = T * Rx * Ry * Rz * S * correction. The correction has no translation or projective terms, so existing position edits and SetWorldPosition keep their local-position semantics. Nonsingular source matrices are factored into editable controls and the retained linear basis; failed or numerically unstable factorization leaves the entity untouched. Source matrices are retained without dropping shear, within float reconstruction tolerance. Singular import input is rejected; ordinary authored zero-scale edits remain possible. World rotation/scale getters retain their existing decomposition semantics and do not promise a unique decomposition under shear.

Scene version 2 adds LINEAR_TRANSFORM records containing entity ID and nine round-trip-precise, column-major floats. Version 2 also writes entity TRS controls at float round-trip precision. Ordinary scenes retain version 1 output. Shared CPU-only scene-format helpers serve loading, snapshots, streaming and read-only validation; unsupported/duplicate scene headers and invalid structural correction records fail rather than being silently salvaged. Affine corrections survive prefab cloning, snapshots and explicit Transform.LinearCorrection variant overrides. Scene save completes format preparation before opening the destination, so compatibility rejection cannot truncate an existing scene. This does not claim atomic file publication or power-loss durability.

Project version 4 is an explicit format opt-in for affine scenes. New projects still default to version 3, and no existing project is upgraded automatically. Project readers/writers support version 4; editor/runtime context carries the declared version. An active older project rejects affine scene reads/writes and affine variant overrides. Detached no-project scene tools can round-trip version 2 data. Runtime marker 4 supports older project versions; version 3 runtimes remain valid for version 3 projects but are rejected for version 4 exports. Source import storage continues to use the version >= 3 Library boundary.

This is a scene representation prerequisite, not generated hierarchy placement. Source/node linkage distinct from scene EntityID, draw-time static binding compensation, animated/skinned ownership, override reconciliation and interactive rendering/physics acceptance remain outstanding. Do not enable generated placement or claim shear-correct collision/gizmo behavior solely because serialization passes.

Validation for the affine slice: editor/runtime/CLI and the asset verification graph built successfully; all 25 focused regressions passed in 6.96 seconds. Prefab cache entries now include the effective project format policy, preventing a cached version-4 affine variant from bypassing compatibility checks in a later older project context. No existing project was converted.
