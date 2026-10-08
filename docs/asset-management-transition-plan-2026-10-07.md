# Asset management transition plan

Date: 7 October 2026 (Europe/London)  
Status: Implementation in progress; last updated 8 October 2026  
Scope: PlutoGE editor asset management, importing, references, model instantiation, migration, and runtime cooking

## Current delivery status — 8 October 2026

| Milestone | Current state | Principal remaining work |
| --- | --- | --- |
| 1. Ownership and compatibility | Core contracts implemented | Final migration policy and legacy support exit criteria |
| 2. References and identities | Stable IDs/catalogs; six asset types opt in to logical writing | Remaining serializers and managed fields; explicit ambiguous-node mapping |
| 3. Import service | Shared CPU service, headless CLI, async editor jobs | General importer extensibility and remaining synchronous entry points |
| 4. Cache and incremental imports | Immutable cache, accepted state, reverse index, reconciliation, debounced watching | Native watcher backends, garbage collection and live-generation leases |
| 5. Source editor workflow | Settings, force reimport, authored extraction/remaps, ownership guards | Undo, broader inspector/import settings, transactional package moves |
| 6. Model prefab hierarchy | Full CPU source topology and transform conventions captured | Generated prefab instantiation, exact scene matrices, ambiguous-node mapping, override reconciliation |
| 7. Cooking/runtime | Logical dependency cooking, runtime catalogs, packed-only loading verified | Remaining converted asset types and broader build integration |
| 8. Existing project migration | Identity audit, confirmed-rename dry run, material preparation and verified recovery-copy service | Conversion coordinator, remaining format writers, rollback and cleanup tooling |

The transition is not complete. New version 3 projects keep imported native products in Library; existing version 1/2 projects retain their co-located layout. The real CoD project has not been migrated or written. Detailed dated progress below supersedes earlier implementation-status notes.

## 1. Objective and scope

Transition PlutoGE towards a Unity-style asset workflow: source and authored files remain in Assets, imported products are regenerated in a disposable Library cache, scenes reference persistent logical identities, and builds contain only the runtime content they need.

The objective is the workflow and ownership model, rather than reproducing Unity's internal file formats or every editor feature. Preserve PlutoGE's renderer, existing native mesh/animation encodings where practical, content pack support, and existing projects throughout the transition.

All eight milestones below are required for the full transition. Milestones 1–4 form the first useful release: stable references and a disposable import cache. Milestones 5–8 complete the editor workflow, model hierarchy, build integration, and project migration. Compatibility adapters and migration tooling must be developed alongside earlier milestones; final project conversion occurs in milestone 8.

### Current implementation baseline

The following observations come from repository inspection and should be checked again before implementation:

- AssetDatabase scans project assets, hashes files, creates .plutometa identity sidecars, discovers dependencies, and supports cooking. Its records already include IDs, importer versions, content hashes, and dependency lists.
- AssetManager resolves project paths and stable IDs, resolves model-local objects through manifests, and caches loaded meshes, materials, and other assets. Some identity resolution currently searches metadata sidecars or derives manifest paths from source paths.
- ContentBrowserPanel.cpp contains substantial model import orchestration, including generated materials, mesh output, animation clips, and final database scanning.
- Model artifacts are co-located with their source package. A legacy Imported/<name> location remains a compatibility fallback.
- A .plutomodel manifest records source identity/hash, importer version, and generated object references. It is an object index, not a complete model scene hierarchy.
- Model object IDs are currently computed from type and name. Renames and duplicate names need stronger identity handling.
- Model importing writes one .plutomesh containing combined geometry, submeshes, skeleton data, animation nodes, and animations. MeshComponent can render a selected submesh range.
- Import options currently include LOD generation, vertex-cache optimization, and overdraw optimization. Mesh metadata carries source and import information.
- Cooking can omit source models while preserving model identity and generated dependencies. Existing tests also deliberately retain directly referenced source models for runtime compatibility.

Relevant starting points:

- engine/assets/include/PlutoGE/assets/AssetDatabase.h
- engine/assets/src/AssetDatabase.cpp
- engine/assets/include/PlutoGE/assets/AssetManager.h
- engine/assets/src/AssetManager.cpp
- engine/assets/include/PlutoGE/assets/ModelAsset.h
- engine/assets/src/ModelAsset.cpp
- engine/assets/src/AssetReferences.cpp
- engine/assets/src/Project.cpp
- engine/import/include/PlutoGE/import/MeshImporter.h
- engine/import/include/PlutoGE/import/MeshImportOptions.h
- engine/import/src/MeshImporter.cpp
- editor/ui/src/panels/ContentBrowserPanel.cpp
- engine/scene/include/PlutoGE/scene/components/MeshComponent.h
- tests/ModelAssetTests.cpp
- tests/LargeMeshOptimizationTests.cpp
- tests/AssetReferenceCookingTests.cpp
- tests/ImportedAssetLifetimeTests.cpp

## 2. Target architecture and invariants

### Project layout

~~~text
Assets/                         Source and authored content; version controlled
    Models/Robot.fbx
    Models/Robot.fbx.plutometa
    Materials/Robot.plutomaterial
    Scenes/Main.plutoscene
Library/                        Disposable; excluded from version control
    AssetDatabase/
    Artifacts/<artifact-key>/
        import.manifest
        meshes/
        materials/
        textures/
        clips/
        model-prefab/
Build/                          Cooked output; disposable
    runtime asset table
    content packs
~~~

The names and manifest encoding are proposed; finalize them in milestone 1. No serialized gameplay reference may depend on an artifact directory or cache key.

### Required invariants

1. Asset identity survives source moves and cache deletion when metadata is retained.
2. Imported object identity survives supported reimports and unambiguous source edits.
3. Every generated output is reconstructible from version-controlled inputs, importer configuration, and supported tool versions.
4. Authored changes never exist solely in Library.
5. Failed imports preserve the previous complete successful generation.
6. Editor loading and runtime loading agree on logical identities, while using separate storage catalogs.
7. Builds do not require the editor cache or original model files for converted assets.
8. Missing, ambiguous, or duplicate identities produce diagnostics; the system must not silently select an arbitrary object.
9. Legacy references remain readable until a verified migration removes the need for their fallback.

### Logical reference model

Introduce AssetReference with a persistent AssetId and a 64-bit localObjectId. Reserve localObjectId zero for the main object. Source paths are presentation and compatibility information, not identity. A sub-object uses its source owner's ID plus its persistent local ID; it does not gain a new independent identity merely because its cache file changes.

An extracted authored asset receives its own AssetId. Cache artifact identity is a separate concept: it identifies one import result for particular inputs and may change on every reimport without changing logical references.

Maintain separate concepts for source metadata, the editor asset index, an import artifact manifest, and a runtime asset table. Sharing schemas or utilities is acceptable, but runtime resolution must not depend on editor filesystem scanning.

## 3. Milestone 1 — Asset ownership and compatibility contract

### Requirements

Classify every registered asset type as source, authored, imported, or a container of objects. Specify who may write it, how it is rebuilt, and whether it may be edited directly. Native meshes and clips may be either authored assets or importer outputs; extension alone cannot determine ownership.

Define versioned formats for source metadata, logical references, import manifests, and runtime catalogs. Establish an engine/project version gate so older editors cannot overwrite newer metadata they do not understand.

### Implementation

- Inventory all serializers, scripting-facing asset fields, inspector pickers, prefab references, animation graphs, particles, shader/material references, and cook roots.
- Record an ownership table and supported reference forms for each asset type.
- Add an explicit origin/ownership field to database records and artifact descriptors.
- Specify read-old/write-new policies and compatibility readers before changing existing output locations.
- Retain existing binary mesh and clip formats unless a later requirement proves a format change necessary.
- Specify metadata preservation rules: unknown fields must survive ordinary edits, and writes must be atomic.
- Decide how engine:// built-ins and project assets share reference APIs without accidentally assigning project identities to engine-owned resources.

### Considerations

Authored materials currently bound to generated meshes must remain authored. A model manifest is not automatically a user-authored prefab. External glTF buffers, texture files, and other import inputs must be accounted for even when they are outside the model file itself.

Do not combine this work with a renderer rewrite, managed resource ownership redesign, shared network cache, or complete Unity feature parity. These can be independent future projects.

### Deliverables and acceptance

An architecture contract, asset ownership inventory, schema/version strategy, and compatibility matrix. Every existing asset type and reference-bearing subsystem has an assigned migration path. Existing projects continue to load unchanged.

## 4. Milestone 2 — Path-independent references and stable object identity

### Requirements

Persist AssetId + localObjectId across scenes, prefabs, materials, animation assets, script fields, and explicitly included build content. Support efficient ID-to-record, path-to-ID, and owner/local-ID-to-object lookup. Paths remain available for diagnostics and legacy references.

Source moves must retain identity when metadata moves with the source. Copying an asset must create a deliberate new identity. Unexpected duplicate IDs must be reported before either asset is silently reassigned.

### Implementation

- Introduce a common AssetReference type and shared serialization helpers.
- Add direct public ID lookup and sub-object lookup to AssetDatabase; publish an index snapshot for consumers.
- Route AssetManager resolution through an asset catalog interface rather than scanning sidecars per lookup.
- Add compatibility readers that resolve existing project:// paths and model identity fields into the new reference representation.
- Adopt new references subsystem by subsystem; retain mixed-format reads during the transition.
- Implement editor move, rename, duplicate, and delete operations with metadata-aware transactions.
- Save persistent sub-object mappings in source metadata. Prefer stable identifiers supplied by the source format; do not assume FBX, glTF, and all exporter versions expose equally stable identifiers.
- For formats without stable identifiers, use saved correspondence and conservative matching using type, hierarchy, previous identity, and source descriptors. Emit diagnostics for ambiguous matches.
- Retain tombstones or equivalent history for removed imported objects so deleted IDs are not accidentally reused for different objects.

### Considerations

The current type/name hash is insufficient for duplicate names and renames. Source node array indices are also not guaranteed stable across exports. Identity matching is a policy with limits: topology or hierarchy changes may require user-directed remapping.

Do not silently make a missing reference resolve to the first mesh or material. Inspector UI should show missing owner/object information and the last known label. Project-relative paths may remain useful for search and debugging without becoming the authoritative serialized key.

### Acceptance

Tests cover moves, renames, duplicate names, copies, deleted objects, missing metadata, duplicate IDs, and source reordering. Existing scene/prefab references resolve through compatibility readers. Repeated loads resolve from indexes without repeated directory walks.

## 5. Milestone 3 — Editor-independent import service

### Requirements

One import service must support the editor, command-line imports, migration, and cooking. The content browser becomes a caller and presenter. Import settings, extraction/remap decisions, and identity mappings must survive Library deletion.

Imports must report progress, errors, warnings, and dependencies. A failed or cancelled job must not replace the last successful result.

### Implementation

- Extract model orchestration from ContentBrowserPanel.cpp into a service outside the UI module.
- Define an importer registry keyed by supported source types and importer version.
- Define an import context containing source identity/path, settings, target configuration, dependency access, cancellation, and diagnostics.
- Define an import result containing named typed objects, stable local IDs, dependency declarations, and artifact descriptors.
- Separate CPU parsing/cooking from GPU resource creation. Command-line import must not require an editor window or graphics context for ordinary model processing.
- Move persisted import options from generated mesh metadata into versioned source metadata. Keep generated metadata readers for compatibility.
- Write outputs into a staging generation, validate them, then publish the manifest/catalog pointer atomically.
- Serialize jobs for the same source and target; allow independent jobs only when importer dependencies and thread safety permit.
- Make metadata updates and artifact publication recoverable after interruption. Record enough state to discard incomplete staging directories safely.

### Considerations

Some current importer APIs expose borrowed mesh/material/animation pointers. Do not assume replacing an import result makes those pointers safe to destroy. Keep publication separate from live resource replacement and preserve the lifetime guarantees exercised by ImportedAssetLifetimeTests.

Importer-owned settings and user overrides must have defined merge rules. External dependency access should be deterministic and recorded; diagnostics should distinguish missing files, unsupported data, importer failures, and invalid configuration.

### Acceptance

Editor and headless import produce equivalent logical objects for the same inputs. Fault injection during parsing, file writing, manifest publication, and cancellation leaves the prior successful import usable. No settings are lost after deleting generated outputs.

## 6. Milestone 4 — Disposable artifact cache and incremental importing

### Requirements

Library must be regenerable and excluded from version control. Imports must invalidate when relevant inputs change and remain reusable when inputs do not change. Startup reconciliation must recover from missed filesystem events.

### Implementation

- Store artifact generations under a content-derived key. Include source bytes, normalized settings, importer/serializer versions, relevant dependency hashes, and target configuration.
- Define a canonical key encoding with explicit field boundaries. Use a collision-resistant digest for artifact addressing; do not treat the existing lightweight file hash as sufficient without evaluating collision requirements.
- Keep logical object mappings separate from artifact keys.
- Persist source inventory, import state, forward dependencies, reverse dependencies, and the active successful generation.
- Distinguish import dependencies from runtime references: an imported material may reference an authored material without requiring mesh geometry to be regenerated on every material edit.
- Add startup reconciliation, change queues, manual reimport, force-reimport, and full rebuild commands.
- Use file size/time as a performance hint where safe, with content hashing as the authoritative input check.
- Debounce save bursts and avoid importing partially written source packages. Changes arriving during import must schedule a follow-up generation.
- Validate manifests and artifact contents before reuse; regenerate missing or corrupt entries.
- Add conservative garbage collection for unreferenced generations after a grace period and after live users release them.

### Considerations

File watching is an optimization, not the source of truth. Case-only renames, Windows path normalization, long paths, dependency cycles, disk exhaustion, and engine upgrades need explicit handling.

Cache reconstruction should preserve semantic content and logical identities. Require byte-identical artifacts only where the importer is deterministic; record unavoidable nondeterminism rather than promising reproducibility that the toolchain cannot provide.

### Acceptance

A fresh checkout with no Library imports successfully. Deleting Library preserves identities and settings. Unchanged startup performs no unnecessary imports. Editing a shared dependency invalidates all required owners and no unrelated imports. Corrupt cache entries recover automatically without deleting authored content.

## 7. Milestone 5 — Source-centred editor workflow

### Requirements

The content browser exposes source assets and their imported objects without exposing artifact paths as ordinary authored files. Model selection provides import configuration and diagnostics. Imported content has clear editing/extraction rules.

### Implementation

- Drive browser items from database records and object descriptors rather than enumerating generated files as independent assets.
- Expand a model into meshes, materials, textures, clips, and its generated model prefab.
- Store selection, drag/drop payloads, and inspector targets using logical references.
- Add import settings with pending edits, Apply/Revert, status, progress, reimport, and contextual errors.
- Add extraction operations that create authored assets with new IDs and save source remaps or prefab overrides.
- Define the scope of remaps: a source-wide material remap differs from a single scene-instance material override.
- Add locate-source, reveal-source, dependency inspection, and missing-reference diagnostics.
- Ensure undo/redo applies to authored settings and remaps, while asynchronous imports consume committed settings snapshots.

### Considerations

Applying settings during an active import should queue a new job or cancel safely. Never let a preview edit mutate a shared generated object without a persistent authored representation. Extracted textures and clips need the same ownership clarity as materials.

Before modifying RML, RCSS, runtime UI controllers, or the RmlUi bridge, read docs/RMLUI_AUTHORING.md and docs/RMLUI_REFERENCE.md and follow the repository scale/layout requirements. Bridge changes require coordinated native/ScriptCore rebuilding and the prescribed interface-scale validation.

### Acceptance

Users can browse imported objects, assign them to scenes, edit import settings, extract materials/clips, and repair missing references without opening Library. Reimport and full cache rebuilding retain committed authored overrides.

## 8. Milestone 6 — Generated model prefab hierarchy

### Requirements

Preserve source node hierarchy, local transforms, mesh bindings, material slots, and animation/skeleton relationships in a generated model prefab. Define instance linkage and override behaviour before enabling automatic structural updates.

### Implementation

- Extend import results with stable node/object identities, parent references, transforms, and render bindings.
- Generate a model prefab representation from those nodes, using existing prefab facilities where their semantics fit.
- Initially retain shared mesh storage and bind nodes to submesh ranges. Separate logical objects from physical buffer allocation.
- Audit the importer for transforms already baked into vertex data; prevent applying those transforms again in the generated hierarchy.
- Represent skeleton/bone bindings explicitly and define which nodes are scene entities versus internal animation data.
- Record prefab-instance overrides against persistent object identities, including material substitutions and supported transform/component changes.
- Implement reimport reconciliation: update importer-owned values, preserve valid overrides, identify removed nodes, and report conflicting structural edits.
- Provide explicit unpacking into authored entities. After unpacking, mesh references may remain shared even though hierarchy linkage is removed.
- Specify interaction with prefab variants, skeleton attachments, root motion, and animation target resolution.

### Considerations

This milestone is separate from moving artifacts into Library. A model prefab is not just a list of meshes, and a scene hierarchy need not allocate one mesh buffer per node.

Source hierarchy changes can invalidate overrides. Handle conflicts visibly rather than silently dropping user edits. Test negative/nonuniform scale, repeated mesh instances, empty nodes, nested transforms, and animated nodes to expose transform-baking mistakes.

### Acceptance

Static multi-part and skinned models instantiate with correct hierarchy and appearance. Supported overrides survive reimport. Deleted/reparented nodes produce defined outcomes. Existing flat model instances remain supported until explicitly converted.

## 9. Milestone 7 — Identity-driven cooking and runtime loading

### Requirements

Cook reachable logical assets from explicit roots and dependency closure. Runtime resolution must work without editor metadata scanning, source model files, or Library. Existing content pack mounting remains supported.

### Implementation

- Build a catalog interface with editor and runtime implementations.
- Resolve startup scenes, script-referenced assets, additional scenes, and explicit inclusion roots into logical references.
- Ensure required imports are current for the build target before dependency traversal completes.
- Generate a runtime table mapping AssetId/localObjectId to packed location, type, and serialization version.
- Distinguish importer input dependencies from runtime object dependencies so source-only inputs are not shipped accidentally.
- Package authored runtime assets and selected artifact outputs; rewrite internal references to runtime identities where required.
- Keep build staging and publication transactional, and version the runtime table/content pack compatibility contract.
- Preserve the direct-source-model fallback during migration, then convert those references and gate removal on project validation.

### Considerations

Static dependency scanning cannot discover arbitrary asset names constructed by scripts. Preserve explicit always-include roots and define how dynamic loading is declared. Keep include-all cooking available for diagnosis, but do not use it to hide missing dependencies in validation.

Current AssetReferenceCookingTests intentionally retain directly referenced source models. Update coverage in stages: legacy compatibility remains tested while new identity-only builds prove source-free loading.

### Acceptance

A pruned standalone build loads reachable scenes, models, materials, clips, particles, and explicit dynamic content from a mounted pack. It excludes unused assets and converted source models. Missing dependencies fail the build with owner/reference diagnostics rather than failing silently at runtime.

## 10. Milestone 8 — Existing-project migration

### Requirements

Migration must preserve identities and authored edits, support a dry run, create recoverable backups, and be repeatable. No generated file may be deleted merely because its extension looks importer-owned.

### Implementation

- Inventory source packages, old Imported directories, co-located artifacts, sidecars, scenes, prefabs, and all serialized references.
- Produce a dry-run report showing proposed mappings, conflicts, missing sources, edited generated assets, and required manual decisions.
- Establish legacy-path/generated-ID to new owner/local-ID mappings before rewriting references.
- Transfer import settings, object mappings, material remaps, and extraction decisions into source metadata.
- Detect modified generated content using prior generation fingerprints where available. When reliable provenance is absent, preserve it conservatively or report a decision; do not infer that it is safe to discard.
- Convert edited generated content to authored assets or supported persistent overrides.
- Import into Library, validate every migrated reference, and compare representative scene/prefab outputs.
- Rewrite project files through their actual serializers, preserving unrelated user edits. Avoid broad textual replacements.
- Record migration version, mapping journal, backups, and completion state so interruption and reruns are safe.
- Remove legacy generated outputs only after loading, validation, and cooking succeed, and only through an explicit cleanup stage.

### Considerations

Missing source files may make regeneration impossible; preserve the native asset as authored content. Same-named source files in different packages, duplicate IDs, renamed generated objects, and unsupported old schemas need explicit conflict handling.

Test migration on disposable copies of real projects, including D:/PlutoProjects/CoD, before changing working projects. Keep compatibility readers for a documented transition period rather than tying their removal to the first successful migration.

### Acceptance

A migrated project retains its material appearance, animations, scene references, prefab behaviour, and build output. Dry run makes no project changes. A second migration is a no-op. Rollback restores the original project. Ambiguous cases remain preserved and reported.

## 11. Delivery sequence and validation strategy

| Milestone | Depends on | Review boundary |
| --- | --- | --- |
| 1. Ownership contract | Existing pipeline audit | Schemas and compatibility policy |
| 2. Stable references | 1 | New references with legacy readers |
| 3. Import service | 1; integrates with 2 | Headless/editor parity and atomic imports |
| 4. Artifact cache | 2, 3 | Disposable Library and incremental imports |
| 5. Editor workflow | 2–4 | Source objects, settings, extraction |
| 6. Model hierarchy | 2, 3; editor integration with 5 | Model prefab and override reconciliation |
| 7. Runtime cooking | 2–4; hierarchy support after 6 | Identity-only packed runtime |
| 8. Final migration | Compatibility work starts in 1; completes after 5–7 | Verified conversion and cleanup |

Keep individual changes reviewable: schema readers first, then writers, resolver integration, import extraction, cache publication, UI adoption, hierarchy, cooking, and final conversion. Feature flags or project format gates should allow comparison with the legacy path while implementation is incomplete.

### Validation layers

- Unit coverage: metadata round trips, unknown-field preservation, identity matching, cache keys, dependency closure, and reference conversion.
- Integration coverage: import failure recovery, filesystem changes, cold-cache rebuild, material extraction, and reimport of live assets.
- Project coverage: real static scenes, large models, skinned characters, animation-only inputs, external textures, and existing prefabs/variants.
- Runtime coverage: pruned packs, missing source files, clean-machine-equivalent loading, and old/new format rejection behaviour.
- Visual coverage: representative static/skinned models on Vulkan and OpenGL, including nonuniform scale, multiple materials, and animation playback.
- Performance coverage: cold import, warm startup, single-asset reimport, dependency fan-out, database lookup, and peak memory. Record baselines before setting numerical budgets.

Extend existing ModelAssetTests, LargeMeshOptimizationTests, ImportedAssetLifetimeTests, and AssetReferenceCookingTests where appropriate. Add targeted test fixtures for actual failure modes rather than mirroring implementation details.

## 12. Main risks and required decisions

| Risk or decision | Required response |
| --- | --- |
| Source identifiers change across exports | Document supported identity guarantees and provide remapping for ambiguous changes |
| Generated materials contain user edits | Preserve/extract before cleanup; persist remaps outside Library |
| Live scenes retain borrowed pointers | Stage publication and preserve resource lifetimes; use lifetime regression tests |
| Combined geometry has baked transforms | Audit import transforms before hierarchy generation |
| Dependency cycles or undeclared dynamic loads | Diagnose cycles; require explicit inclusion for dynamic runtime content |
| Old editors rewrite new metadata | Project/schema compatibility gates and preserved unknown fields |
| Cache corruption or interrupted writes | Validated manifests, atomic publication, last-good generation |
| Platform/importer output differences | Target-aware keys and explicit importer/tool versions |
| Duplicate metadata IDs | Report conflicts and distinguish deliberate duplication from accidental collision |
| Library deletion loses object mapping | Persist identity maps and settings with source metadata |

Before implementation, finalize manifest encoding, supported AssetId representation, reference serialization syntax, source-object matching policy, imported material defaults, model override scope, and the duration of legacy-format support. These decisions belong in milestone 1; they do not prevent drafting the interfaces and inventory now.

## 13. Overall completion criteria

The transition is complete when:

- Assets contains source and authored content with all required metadata under version control.
- Library can be deleted and reconstructed without loss of settings, identities, or authored edits.
- Scenes and prefabs resolve persistent logical references independently of physical artifact paths.
- Model assets expose imported objects and generate correctly linked model hierarchies.
- Reimport preserves supported overrides and reports conflicts.
- Headless importing and cooking work without editor UI or ordinary model GPU initialization.
- Standalone packs resolve converted assets without source models or the editor cache.
- Existing projects migrate repeatably, with validated backups and no silent data loss.
- Legacy removal occurs only after migration and runtime compatibility gates are satisfied.

## Implementation progress — 8 October 2026

Started milestones 1 and 2 with the [initial ownership and identity contract](ASSET_IDENTITY_AND_OWNERSHIP.md), public indexed asset-ID lookup, and safe database snapshot publication. Duplicate IDs and existing unreadable metadata now reject scanning without silently rewriting identities. Regression coverage exercises conflict handling, failed-scan pointer stability, metadata preservation, recovery, and source rename with metadata.

This is a foundational slice, not completion of either milestone. Asset paths, scene serialization, generated file locations, runtime formats, and importer orchestration remain compatible. Metadata transactions, logical sub-object references, persistent correspondence, and the remaining migration work are still pending. Validation results are recorded after running the targeted build and tests.

Validation: RelWithDebInfo builds of PlutoGEAssetReferenceCookingTests and PlutoGEModelAssetTests succeeded. Both targeted CTest cases passed; git diff --check passed. The first sandboxed build failed in MSBuild file tracking before compilation; the approved build retry succeeded. No project migration or asset relocation was performed.

### Continued work — 8 October 2026

Implemented a renderer-independent metadata parser/writer, a strict logical-reference codec, a storage-neutral object catalog, retained catalog snapshots, and catalog-aware resource resolution. Extracted CPU material serialization and headless model import orchestration into PlutoGE::AssetImport; the editor delegates to the shared service and PlutoGEImportModel exposes it to CLI callers. Imports stage outputs, protect unrelated authored collisions, preserve material overrides, and journal legacy publication for rollback/recovery outside disposable Library.

Added the version 2 project gate, persisted source import options, correspondence/tombstone records, source-owned material remaps, and source-rename handling. Existing version 1 projects retain legacy serialization. Added a cache-bypass policy and external input discovery to the low-level mesh importer. These changes advance milestones 1–3; they do not yet complete logical-reference adoption across all serializers or the Library/cache/hierarchy/runtime/migration stages.

New regression targets: PlutoGEAssetMetadataTests, PlutoGEAssetCatalogTests, PlutoGEModelImportSettingsTests, PlutoGEImportFileTransactionTests, and PlutoGEModelImportServiceTests. Final integrated verification is in progress. Detailed boundaries and limitations are maintained in ASSET_IDENTITY_AND_OWNERSHIP.md.

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

### Validation and reference serialization — 8 October 2026

Thirteen targeted regression cases passed for metadata, catalog, dependencies, validation, transactions, cache, model settings/import, cooking, mesh LOD/large meshes, and borrowed-resource lifetime. Subsequent catalog reverse-location lookup and nested output-parent transaction checks also passed. A copied real M16 GLB imported successfully through the headless CLI in an isolated version-2 project: approximately 1.0 seconds cold and 0.5 seconds warm on this machine. The original CoD project was not modified. This found and fixed Windows resolution of not-yet-existing nested output directories.

Catalog reverse lookup prefers a unique source-owned subobject; ambiguous shared outputs fall back to a unique standalone main identity, otherwise path persistence remains unchanged. Opt-in native mesh/material serialization and runtime compatibility gating are being verified. These are staged adoption boundaries: texture and other asset writers, managed fields, generated-output relocation, editor settings/extraction, hierarchy and project migration remain outstanding.

### Editor settings and identity-preserving moves — 8 October 2026

The version-2 editor now enables native mesh/material logical serialization after a validated catalog scan. Content-browser refresh and mutation actions republish that catalog through a shared EditorShell method. Scene round-trip tests cover native mesh identity preservation after rename; export rejects runtimes without the required asset-pipeline marker before writing output. All thirteen focused regression cases passed after this integration.

A reusable AssetMoveService now handles browser renames with the import project lock, canonical asset-root containment, exclusive destination publication, exact identity-sidecar preservation, authored mesh material override moves, and ordinary-error rollback. Existing assets and orphan destination sidecars are never overwritten. Regression cases cover identities/unknown metadata, occupied destinations, orphan sidecars, external paths, active import locks, mesh overrides, and folders. These tests passed. Multi-file moves are not yet crash-recoverable; this limitation is explicit in the API rather than an atomicity guarantee.

ModelImportService exposes a read-only effective-options query sharing the same resolver as Import. The source details panel offers draft LOD/vertex-cache/overdraw options with Apply Settings and Reimport, plus Revert Settings. Failed imports keep drafts and committed source data intact; ordinary Reimport consumes committed settings. Legacy projects retain options through mesh metadata; version-2 projects persist source settings. Query tests verify defaults, persisted values, no project writes, and unchanged output on invalid queries. The editor/runtime build and focused importer tests passed; final missing-source and selected-entry lifetime hardening is being verified. Asynchronous jobs, progress/cancel UI, undo for committed asset settings, broader settings, and extraction service modernization remain pending.

### Request-index optimization — 8 October 2026

ArtifactCache now supports immutable per-request generation hints under Library/Artifacts/Requests-v1. Model imports use source-owner IDs as request identities. Warm lookup checks that owner's known generations before full enumeration; missing, stale, malformed or unavailable hints fall back to authoritative cache search and can rebuild the hints. Multiple publishers add independent marker directories rather than replacing a shared mutable index. Request buckets use 128 hash bits to reduce Windows path length; collisions only add candidates because full SHA-256 generation validation and request predicates remain authoritative.

Focused cache tests passed for unrelated-generation avoidance, missing-index fallback, malformed/stale hints, and unchanged outputs on misses. A Windows path-length regression was found and corrected during verification. Full integrated rebuilding and verification are in progress. This is a request lookup index, not the reverse dependency graph, latest-generation publication model or garbage collector still required by milestone 4.

### Background imports and hierarchy groundwork — 8 October 2026

ModelImportTask now owns copied project/request snapshots on a worker, exposes synchronized state/progress, rejects replacement of active work or unconsumed completions, supports cancellation, and joins before owned state is destroyed. The editor starts source-details imports/reimports asynchronously and polls completion in its frame loop. GPU lookup refresh and catalog publication remain on the editor thread. Context switches request cancellation and discard results belonging to another project. Cancellation is stage-based during parser execution. External source-copy/import entry points still use the synchronous adapter and reject concurrent imports.

ProjectAssetLock moved into Assets so importing and cooking share project ownership. Cooking acquires the OS lock before scanning or destination writes; a regression verifies rejection while an import owns the project. Editor export rejects an active background import. Whole standalone-export publication remains a separate transactional-build requirement.

ImportedModelHierarchy records full source nodes, selected scene roots, local/world matrices, node-to-submesh bindings, exact baked geometry transforms, draw-time transform-node indices, and skinning flags. Source indices are correspondence inputs rather than persistent IDs. A renderer-independent iterative builder handles forward parents and deep graphs, rejects invalid parents/cycles/non-finite matrices, and preserves its previous output on failure. glTF source topology is resolved independently from its existing selected-scene runtime transform table. FBX geometry remains in node space; glTF records its actual static/animated baking convention. Geometry/render behavior is preserved.

Tests cover empty and unselected nodes, negative/nonuniform scale, repeated FBX instances sharing geometry, forward parents, 10,000-node hierarchy construction, invalid graphs, async cancellation/snapshot ownership, and existing FBX/skinning behavior. All sixteen targeted CTest cases and the RelWithDebInfo editor/runtime/CLI build passed. A fresh isolated cold import of the copied real M16 GLB also passed with complete hierarchy capture. The legacy source-stamp cache does not serialize this new topology; fresh/bypass parsing provides it. Persistent node correspondence, generated prefab serialization, scene instantiation and override reconciliation remain pending milestone 6 work.

PlutoGEAssetPipelineChecks now builds the verification graph in one invocation, sharing engine dependencies across test/editor targets. Project-root asset-layout exclusions for Library, locks, import/metadata staging and source control are being verified next; those files must remain outside logical asset ownership even when assetDirectory is '.'.

### 2026-10-08 — Independent authored mesh extraction and ownership enforcement

- Added CPU-only native mesh decoding to AssetManager. Extraction no longer needs a renderer or live mesh resource. Failed decoding leaves caller outputs unchanged. Staged mesh saves verify stream close before returning success.
- Added an extraction service in the asset-import module (subsequently generalized as ModelObjectExtractionService). It takes the project lock, recovers interrupted import transactions, resolves a freshly scanned imported mesh identity, stages a new mesh and a new sidecar, checks source and authored binding digests, publishes both files, validates the resulting catalog, and accepts the transaction. Collisions, orphan companions, path escapes, and provenance mismatches are rejected. The returned result is published only on success.
- The extracted mesh clears source reference, source owner ID, and source local object ID. EXTRACTED_FROM records historical origin in metadata without creating authoritative model linkage. Geometry, skeleton, animation nodes, import options, and effective material bindings are retained. Material dependencies remain shared references; this is a mesh copy rather than a recursive dependency clone.
- Explicit optional OWNERSHIP records now distinguish newly authored copies from legacy unclassified main assets. Supported values are AUTHORED and SOURCE; imported ownership remains derived from model manifests. Existing sidecars without this record keep their previous interpretation. Duplicate and unsupported ownership records fail validation, and unrelated extension records survive edits.
- AssetManager rejects direct mesh, material, clip, and animation-set saves to imported catalog locations, including logical and physical aliases. Controlled importer writers use a private manager without the editor catalog. Authored material binding overrides remain separate. Mesh and material editor save buttons explain the extraction requirement.
- Removed MeshEditor canonical writeback, which previously made edits to extracted copies overwrite their imported source object. Native authored meshes without source object metadata can now be placed in scenes.
- First build and four focused regression tests passed before the explicit ownership extension; the combined change subsequently built successfully and all 16 asset pipeline regression tests passed. No real project assets were migrated. Remaining work includes transactional material extraction/remapping, dependency cloning policy, legacy extracted-copy repair, and Library-only active artifact storage.

### 2026-10-08 — Transactional native object extraction and material remaps

- Generalized mesh extraction into ModelObjectExtractionService for native mesh, material, texture, animation-set, and clip objects. Every extracted file receives a new authored main identity and historical origin; mesh copies additionally remove authoritative source linkage. Dependencies remain shared rather than recursively cloned.
- Extract-and-use for a material now publishes its new native file, sidecar, affected mesh override files, and (for version 2 projects) the source material remap together in a recoverable transaction. Remaps persist the authored main identity rather than a destination path. Source bytes, effective bindings, optional missing overrides, and source metadata are checked for concurrent changes before publication. Existing generated material bytes remain untouched.
- The browser publishes the returned catalog and refreshes changed resource lookups on the owning thread. Existing scene material references are updated for both path and logical aliases. Extraction of all objects remains a sequence of per-object transactions with explicit partial-success reporting.
- Catalog publication now applies to both legacy and version 2 editor projects so imported ownership can protect direct saves in either format. Logical-reference writing remains enabled only for supported types in version 2; opening a legacy project does not upgrade its manifest version. Invalid scans retain the previous catalog and report an error.
- The CPU integration test verifies persisted remaps, unchanged generated material bytes, independent authored ownership, mesh binding updates, and successful cold reimport after deleting the isolated test project's Library directory. Native tests passed; final editor rebuild follows the last adapter fix.

### 2026-10-08 — Recovery before catalog consumption

- Editor refresh holds the project asset lock and recovers interrupted publications before scanning. This applies to legacy and version 2 projects without enabling legacy logical writers.
- Standalone cooking rejects pending, symlinked, or unreadable transaction infrastructure before scanning or creating destination output. Assets cannot depend on AssetImport for recovery without a dependency cycle; recovery remains in the importing/editor layer. This guard avoids cooking an incomplete legacy multi-file publication.
- Extraction converts filesystem exceptions into diagnostics, and allocates its completed result before acceptance so post-commit publication uses move assignment.
- Legacy and version 2 material extraction/remap tests both pass. The recovery guard initially exposed a fixture that intentionally left a staged directory behind; the test now verifies cook rejection before clearing that isolated fixture and testing infrastructure exclusion.

### 2026-10-08 — Explicit rebuild commands

- ModelImportRequest now distinguishes normal reimport from force reimport. Normal requests can reuse validated generations. Forced requests bypass warm restoration, parse the current source package, and retain deterministic output validation and transactional publication. Identity correspondence and committed authored remaps remain in force.
- The model inspector exposes Force Reimport through the existing asynchronous task. It uses committed settings, while Apply Settings and Reimport continues to consume the draft settings snapshot.
- PlutoGEImportModel accepts --all for deterministic source inventory traversal and --force for full rebuilds. Each source is an independent transaction; failures are reported per source, processing continues, and the command returns failure if any source failed. The command does not delete Library or authored content.
- Validation: engine/editor build passed; the integration test proves a forced unchanged import parses without warm reuse while preserving native bytes and authored copies. The isolated M16 project successfully completed --all --force with one imported source and zero failures. No real CoD project content was written.

### 2026-10-08 — Accepted import state and dependency reconciliation

- Cold and warm successful imports now capture accepted input digests after canonical settings/override publication. Non-authored dependencies must still match their original snapshot before acceptance. Optional atomic bookkeeping persists owner ID, source reference, generation key, and accepted inputs under Library/ImportState. Failure to save disposable state does not turn an accepted import into a failed operation. Missing or corrupt state triggers rebuilding; corrupt ordinary cache records are quarantined.
- Added bounded import-state codecs using the shared atomic metadata envelope. Required source/source-metadata inputs, unique input identities, absolute paths, valid generation digests, and schema markers are validated. Failed parsing and serialization retain caller outputs.
- ReconcileModelImports inventories model sources without parsing models, creating sidecars, or writing project content. It checks input hashes, importer/target version, artifact integrity, and published native output. PlutoGEImportModel --check reports current, needs import, or blocked; exit codes are 0 for all current, 3 for required imports, 1 for blocked/errors, and 2 for invalid command usage.
- ImportDependencyIndex builds a replaceable reverse index from accepted inputs, separately from runtime references. Shared input changes select all owning sources exactly once; unrelated runtime material changes select none. PlutoGEImportModel --affected accepts an absolute input path. Missing state conservatively selects its owner. This supplies scheduling primitives; automatic watching, debouncing, and editor startup queueing remain subsequent integration work.
- Regression checks cover unchanged read-only reconciliation, native output damage/restoration, state codec failure invariants, shared dependencies, unrelated runtime references, failed reverse-index replacement, and invalid disposable state rebuilding.

### 2026-10-08 — Persistent generated-file provenance and strict model manifests

- Version 2 imports now retain SHA-256 baselines for generated native files in their model package manifest, excluding authored binding overrides. The model artifact recipe version is now 3 because manifest bytes changed. These baselines survive Library deletion. They must move into source-owned metadata when model manifests move into disposable Library storage. Legacy packages without baselines remain conservatively outside this new proof of unedited content.
- Reimport, including force reimport, rejects modified generated files whose edited generation has not been preserved. Material extract-and-use saves EXTRACTED_FROM and EXTRACTED_CONTENT_HASH in the authored copy. A matching persisted source material remap proves the precise edited generation was preserved before the original generated file can be regenerated. The integration test edits a generated material, verifies rejection without byte changes, extracts/remaps it, deletes Library, and successfully reimports.
- Model manifests now have bounded, transactional CPU codecs with exact numeric parsing, duplicate object/field rejection, valid digests, CRLF support, and unknown-record preservation. Save validates before opening the destination and checks stream close. Legacy minimal manifests with inferred source ownership and Unknown object types remain readable; unsupported type names are rejected.
- Legacy Imported/name fallback selection now checks ownership instead of associating unrelated source files that share a basename. A real-project read-only audit exposed that case for the duplicated animation-pack source; the corrected audit no longer reports a false ownership conflict.
- Validation: engine/editor build succeeded and all 16 asset pipeline regressions passed. Read-only CoD reconciliation found 19 model sources with absent new import state, including Soldier_Rig_Review.glb without a source sidecar. It wrote no project assets, settings, metadata, or cache. This is inventory and compatibility verification, not migration approval or completed legacy-edit preservation.

### 2026-10-08 — Startup reconciliation and live mesh generations

- Version 2 editor projects now reconcile source packages asynchronously after opening or a successful catalog refresh. Workers own project snapshots, read accepted state under the project lock, and queue only new sources or packages with verified generated-file baselines. Legacy packages without proof of unedited output remain manual review work. Cancellation and project-context epochs prevent stale completions from publishing into a reopened or different project.
- The editor consumes completion and publishes catalogs/resource changes on its owning thread. Source-specific diagnostics prevent a later queued import from being shown as the result of an earlier inspector request. External source imports share the same publication path. Play waits for asset processing; model import entry points reject publication during Play.
- Added a scene-layer generation reconciliation API independent from the editor. It replaces imported mesh bindings using source owner/local identity, retains the previous borrowed generation for removed or unresolved objects, and reports repair diagnostics. Borrowed old resources stay alive. Animation pose and binding caches are invalidated while authored animation state is retained.
- Default material inheritance is compared against the previous catalog and borrowed material pointers. Inherited defaults adopt changed source bindings; explicit material instances, submesh overrides, pivots, and mesh ranges survive. Material instance serialization now records the instance configuration even when an old asset reference remains attached.
- Regression coverage verifies three live mesh instances, a changed default, explicit cloned material and submesh overrides, initially missing geometry, retained removed-object geometry, and pose-cache invalidation. Full animation clip-list regeneration, topology-aware submesh override reconciliation, generated prefab instances, and continuous filesystem watching remain outstanding.
- Broader verification includes material reload and prefab export/variants. It exposed Windows weakly_canonical failures for nonexistent save destinations and packed-only prefab paths. Prefab identity comparison now resolves the existing parent prefix through the shared filesystem helper, retaining canonical aliases without requiring a loose packed file. Final broader verification follows this fix.
- Directory moves currently preserve sidecars and files but do not rewrite all embedded source/output/dependency paths. Do not treat directory rename as a completed reference migration. Generated output still has native Assets locations plus immutable Library copies; true Library-only storage remains pending.

### 2026-10-08 — Source-owned package snapshots

- Added ModelSourcePackage in Assets, using the existing bounded model codec inside versioned metadata records. It persists source owner, object local IDs/types/names/locations, importer version, native output hashes, and unknown package records in the source sidecar. Unrelated source metadata survives updates. Duplicate headers, unsupported schemas, owner mismatches, invalid package content, and escaping project locations fail without replacing caller output or metadata. Metadata retains its existing 1 MiB envelope limit; oversized packages fail publication rather than silently losing provenance.
- Version 2 imports publish this snapshot in the same transaction as native output and importer settings. The artifact recipe version is 4 because the source metadata output changed. Native manifests remain compatibility copies in Assets for this stage; output relocation has not yet happened. Legacy sources without package snapshots continue to use existing native manifests.
- Catalog scans prefer source-owned snapshots over native manifest copies. Browsing and model placement use a shared read-only package loader. Import and reconciliation can recover a missing native manifest from source metadata while retaining baseline edit protection. This removes one prerequisite for eventual Library-only generated manifests without introducing an Assets-to-AssetImport dependency.
- The regression deletes both Library and the current native manifest, verifies catalog identities remain resolvable, rejects modified generated geometry with no additional writes, restores the unchanged geometry, and rebuilds byte-identical authored/native files and metadata. Package codec tests cover unknown records, ownership, duplicate/future headers, and failure invariants. All 19 asset regression cases passed after this integration.
- Correspondence consistency is additionally being verified: every previously published local object must still have an active source correspondence entry before import or automatic scheduling. Missing/incomplete tables must request repair rather than allocate replacement identities.

### 2026-10-08 — Debounced source watching and ownership-safe moves

- ImportWatch captures read-only timestamp/size/type hints on a worker for model sources, sidecars, accepted external inputs, authored mesh binding overrides, generated-file baselines, native manifests, and Library existence. Project inventory discovers added/removed sources. Failed or cancelled capture retains the previous output. These are event hints, not a substitute for content hashes; timestamp/size-preserving external edits still require explicit Refresh, --check, or reimport.
- ImportWatchDebouncer is independent of filesystem and editor code. First observation establishes a baseline; repeated matching observations after the quiet period emit each changed path once. Continuous writes reset the delay and reverted bursts disappear. The editor polls at roughly one-second intervals while idle in edit mode and runs authoritative reconciliation only after stable changes. Import queues serialize work; edits during a job are observed afterward.
- Worker completions carry project context epochs and cancellation state. Initial watch establishment requests an audit after the baseline to close the startup gap. Explicit cancellation resets the baseline without immediately rescheduling the same request. Watching is paused during Play, imports, and reconciliation. A failed external lock does not mutate project content; repeated identical watch errors are suppressed.
- Tests cover read-only capture, cancellation failure invariants, cache/manifest deletion, quiet-period emission, reverted bursts, and reset isolation. The editor build and focused package/import regressions passed. Native notification backends and reverse-index filtering of reconciliation remain possible optimizations; current changed snapshots trigger a full authoritative model assessment.
- MoveProjectAsset now rejects independent imported-object/manifest moves and directories containing imported model packages until transactional embedded-reference migration is implemented. Generic authored moves and standalone source file moves remain supported. Reserved project infrastructure cannot be moved into or out of the authored tree, including root asset layouts. Windows comparisons account for path case and canonical short/long root aliases. Filesystem inspection exceptions become diagnostics.
- New move regressions verify byte-preserving rejection for package folders, generated native files, Windows casing aliases, and root-layout Library targets. A Windows short-path mismatch initially exposed an infrastructure bypass in the new guard; canonical project-root comparison corrected it, and the regression passes. No real CoD content was modified.

### 2026-10-08 — Logical native dependency writers

- Version 2 editor persistence now opts in textures, animation sets, clips, and animation graphs alongside meshes/materials. Legacy contexts still leave logical writing disabled by default. Existing logical URIs and built-in references remain unchanged.
- Authored native mesh writers normalize material dependencies through the catalog. Animation sets normalize clip references; animation graph states, blend-space points, layers, and referenced layer graphs persist identities. Graph normalization uses a copy and retains caller-owned state. Material writers normalize project-owned textures and shader texture override references; their legacy texture-relative format remains the fallback without a supported catalog identity.
- Animation set, clip, and graph saves now explicitly close/check their streams before reporting success or publishing cache entries. Imported-object save protection remains in force. Private importer writers retain the current native path layout until source generation reference planning is implemented.
- End-to-end regression passed: it writes authored native dependencies, renames clip/texture/material files with their sidecars, rescans, performs a pruned cook, mounts packed-only runtime content, and loads animation sets, graph dependencies, textured materials, and mesh bindings through logical references. Other serializer/managed asset types and Library-only generation storage remain pending.

### 2026-10-08 — Stable generation identity and publication preconditions

- Model artifact recipes now distinguish content inputs from authored publication preconditions. Raw source sidecars and mesh override files remain snapshotted and checked before publication, while effective settings, object correspondence, unknown semantic records, and resolved bindings determine generation identity. Accepted import state retains both categories for later reconciliation and watching.
- Generated source-package records and the sidecar importer-version field no longer feed their rewritten bytes back into the next generation key. Unknown package extension records remain semantic inputs because they affect preserved output bytes. The recipe version is 5; generic ArtifactCache storage remains independent of model-specific metadata rules.
- This removes a prerequisite for future active-generation keys in persistent metadata: unchanged force reimport after the first successful version 2 import now produces the same generation key and identical native/authored bytes. A regression verifies that invariant alongside existing concurrent sidecar/override edit rejection. Focused importer tests passed; final combined verification follows.
- Native logical-reference adoption also passed all 19 regressions and a cold forced import of the copied real M16 source in an isolated project. Renamed clip, texture, and material dependencies survived pruned cooking and packed-only loading. Existing files require explicit save/conversion; logical writer adoption does not silently rewrite the real project.

## Implementation continuation — 8 October 2026: logical generated dependencies and read-only scans

- Version 2 model imports now encode generated mesh material bindings, material texture bindings, and animation-set clip bindings with persistent source-owner/local-object identities. Physical locations remain catalog data. Version 1 imports preserve their existing encoding. Artifact recipe version 6 prevents incompatible native bytes from sharing an earlier cache generation.
- Authored material overrides persist as main-object identities. Warm-cache matching resolves both physical compatibility paths and logical bindings through the same catalog; imported subobjects require extraction before becoming authored overrides. Source renames retain imported identities and authored remaps.
- AssetDatabase supports read-only identity scans: absent sidecars leave unidentified records, and malformed or duplicate identities reject the candidate snapshot. Independent options skip hashing and dependency reads for identity-only consumers. The importer uses a private read-only catalog for dependency resolution; ordinary editor scans retain their existing defaults.
- Validation: editor/runtime/CLI aggregate build succeeded; all 19 asset-pipeline regression targets passed, including direct generated material/clip identity assertions, unchanged forced generation keys, authored remap warm reuse, source rename, packed-only loading, and resource lifetime checks.

## Implementation continuation — 8 October 2026: migration identity audit

The shared read-only `AuditAssetMigration` service and `PlutoGEImportModel <project> --audit` inventory persisted identities without creating sidecars, lock files, cache entries, or recovery writes. Reports include missing identities, invalid metadata, orphan sidecars, symbolic links excluded from traversal, and duplicate IDs including orphan metadata. Output ordering is deterministic; enumeration failures and cancellation preserve the previous report. Exit codes are 0 for no findings, 3 for warnings, 1 for errors, and 2 for usage errors. This is an advisory identity audit, not a completed conversion preflight or permission to migrate.

Read-only CoD audit: 825 recognized assets, 1,140 sidecars, three findings. Soldier_Rig_Review.glb lacks metadata. SourceModels/grass_green/scene.gltf.plutometa is orphaned and shares its ID with SourceModels/scene/scene.gltf.plutometa. Neither identity was changed. The report is saved locally under out/cod-asset-migration-audit-2026-10-08.txt. Conversion must resolve this conflict with the user, validate references and importer correspondence under a publication lock, create backups, and provide rollback before writing the real project.

## Implementation continuation — 8 October 2026: dry-run mapping and acceptance validation

`PlanAssetReferenceMigration` and `--plan-migration` report supported serialized reference occurrences and their existing catalog identities without changing files. Files carry SHA-256 input hashes, generated ownership, proposed logical mappings, and unresolved-reference/scan diagnostics. Duplicate/invalid metadata produces an audit-only report rather than guessed mappings. This does not apply textual replacements, assign missing identities, establish complete serializer coverage, or claim migration readiness. Actual conversion still requires format-specific serializers, backups, locked revalidation, authored-edit preservation, and rollback. Fixture tests cover unchanged files, orphan duplicate blocking, resolved identities, missing targets, and cancellation. CoD correctly produces no mappings until its identity conflict is resolved.

Cold imports and warm restores now run shared final artifact publication validation immediately before accepting their transaction. Content inputs and every published output must still match the immutable generation; output symlink traversal is rejected. Late callback edits to generated geometry fail and roll back on both paths while preserving the previous result/catalog. Editor/runtime/CLI rebuilding and focused importer/cache tests passed. This narrows the external-edit race; writers outside the project lock can still race an individual final filesystem operation, and power-loss durability remains outside the current transaction guarantee.

## Implementation continuation — 8 October 2026: indexed editor reconciliation

Debounced events now carry changed absolute paths into background reconciliation. When every event maps to a known accepted import input, expensive content/cache/output checks run only for its owners and sources with missing state. Source metadata inventory still checks global duplicate-owner counts. Unindexed events, generated-output changes, deleted cache directories, invalid index inventory, startup, and explicit refresh conservatively use the full authoritative audit. Cancellation/context changes clear pending event scope.

Focused regression coverage passed for two independent source owners, exactly one selected owner for an indexed source edit, full fallback for a generated output, unchanged caller output on invalid paths, and no project writes. Native filesystem watcher backends and generation-based garbage collection remain future work.

## Implementation continuation — 8 October 2026: Library-backed storage and cooking boundary

AssetStorageMap separates a generated object’s virtual project-relative location from its physical editor input and expected SHA-256 digest. AssetDatabase verifies imported ownership, canonical project Library containment, ordinary file type, and generation hashes before publishing a normalized immutable storage snapshot. Missing generated Assets records are synthesized from persisted source-owned objects without inventing native-file identities. Failed scans retain the previous catalog and storage snapshot. AssetManager resolves logical/virtual references through that storage snapshot and persists mapped physical dependencies back to identities; project context changes clear storage. CookOptions accepts the same mapping, and the cooker copies physical Library inputs to ordinary relative runtime locations. Default imports and project opening still use co-located outputs: active-generation pointer publication and full Library-only default activation remain outstanding.

The Library-only integration fixture deletes generated mesh/material/texture files from Assets, resolves CPU geometry from immutable Library artifacts, cooks its generated dependency graph, rejects corrupted bytes and invalid storage overrides, and preserves virtual runtime locations. The first test exposed glTF percent-encoded URI handling; URI dependencies now decode escapes, preserve literal JSON ampersands, and report malformed/control-byte/escaping paths.

Reference occurrences now distinguish generated mesh source provenance from runtime dependencies. Current version 4/5 native mesh trailers are recognized without allocating geometry; unknown future trailers remain conservative runtime references. Reference queries can still report source provenance, while cooking, runtime validation, and migration mapping omit it. Direct source glTF runtime roots retain source and external buffer dependencies. Focused importer, reference, validation, and cooking tests passed after these corrections. Packed-only loading also passed: loose cooked assets were removed, and runtime catalog lookup loaded geometry plus logical material/texture dependencies without materializing files.

## Implementation continuation — 8 October 2026: active Library generations and new-project defaults

Newly created projects now use format version 3. Shared cold-import and cache-restore publication policy writes only source metadata and authored mesh material overrides into Assets; generated native geometry, materials, embedded textures, clips, animation sets and the native index remain in the immutable Library generation. Version 1/2 imports retain their existing layout. No existing files are deleted or projects upgraded automatically. Recipe version 7 includes the storage policy in its semantic fingerprint, preventing different publication layouts from sharing a generation key.

Source sidecars persist a strict bounded MODEL_ARTIFACT version 1 record selecting the generation. Database scans reconstruct storage from that pointer and persisted object/hash correspondence without relying on disposable import state or native manifests in Assets. The derived pointer is excluded from semantic settings to avoid generation-key feedback, and is emitted in a deterministic position after settings/package records. A force-import regression caught and verified that ordering requirement. Unknown metadata remains preserved; malformed, duplicate and future pointer records fail conservatively.

Editor recovery scans retain catalog identities and expected physical routes when Library files are missing or corrupt, but mark those routes unavailable. AssetManager returns no physical path for an unavailable route and does not fall back to stale generated Assets files. Strict cooking/scans still reject invalid inputs. Accepted import state and reconciliation distinguish disposable cache bytes from authored native edits; missing or corrupt Library can rebuild from source settings and object correspondence. Authored overrides are read separately from geometry and survive complete Library deletion. Active Library files participate in debounce inventories, with unindexed events using authoritative full reconciliation.

Catalog and storage publication now occurs together on the owning thread. When a material identity moves to a different physical generation, unambiguous cached material routes rebind and reload the same borrowed object; scene-owned material copies retain their configuration. Missing-generation snapshots preserve the last physical route for later recovery. Ambiguous shared routes retain last-good borrowers. Authored mesh material overrides always resolve under canonical Assets paths, including CPU metadata lookup and extraction; canonical containment uses the shared platform path policy.

Final acceptance checks both immutable cache integrity and the subset published into Assets. Late cache corruption rejects the import and rolls back authored publication while retaining the previous result/catalog. Orphan generations may remain after failed publication; garbage collection and live-generation leases are still outstanding. Runtime exports advertise pipeline version 3; version 2 runtimes remain valid for version 2 projects and cannot export version 3 projects. Runtime catalogs and packs contain virtual relative locations, with no Library paths.

Validation: editor, runtime and headless CLI builds succeeded. All 19 asset-pipeline regression tests passed. Dedicated version 3 coverage uses Project::Create, cold/warm/forced imports, unchanged sidecar bytes, source-only Assets output, default cooking, removal of loose cooked files and packed-only geometry loading, current reconciliation, deleted-Library reconstruction, late cache corruption rollback/rebuild, and authored material extraction/remapping followed by another full Library deletion. Existing version 1/2 tests remain passing. Material lifetime coverage verifies pointer identity across generation relocation and an unavailable intermediate snapshot, preservation of a private copy, and separation of old physical generations. CoD remains untouched.

## Implementation continuation — 8 October 2026: persistent node correspondence foundation

Version 3 cold imports now persist conservative node source keys and local IDs in the existing bounded model correspondence records, sharing the native-object ID allocator and its retired tombstones. ReconcileModelNodeIdentities is a CPU-only asset module independent of scene entities, UI and renderer resources. Source indices address the current snapshot only. A producer can supply persistent source identifiers; duplicate identifiers remain unresolved. Current adapters use unique named parent paths hashed into bounded versioned keys. Hash framing prevents delimiter aliases, and iterative parent traversal supports forward parent indices and deep hierarchies.

Reordering nodes, adding distinct siblings and editing transforms preserve IDs. Without producer identifiers, renaming or reparenting creates a new identity and retires the previous key; restoring the exact key reactivates its tombstone. Duplicate sibling names, anonymous nodes and descendants of unresolved parents receive no guessed ID. Producer identifiers can resolve anonymous/duplicate names and remain stable across rename/reparent. A future editor mapping workflow must expose those unresolved cases before generated prefab override reconciliation relies on them. Node keys reserve correspondence IDs; they do not create ordinary asset catalog objects or prefab entities.

Recipe version 8 covers this metadata/output change. Tests verify reordering, transform changes, insertion, rename/restoration, cross-kind ID uniqueness, metadata round trip, duplicate/anonymous paths, persistent producer identifiers, malformed/cyclic topology with unchanged outputs, and a 2,000-node forward-parent chain with bounded keys. Version 3 integration asserts persisted node correspondence and unchanged cold/force/cache-rebuild sidecars. Editor/runtime/CLI builds and all 19 pipeline checks passed. An isolated copy of M16.glb imported with only the source and sidecar in Assets, persisted 23 node IDs, reused the cache on the next import, and reported current through --check. No original CoD files were changed.

Generated prefab serialization and instantiation remain outstanding. Source matrices and baked/animated/skinned binding provenance are captured, but scene entities currently expose TRS transforms and mesh components apply their own draw-time node transforms. The next stage must preserve exact matrices and compensate existing vertex baking/animation transforms; simply assigning source node transforms to existing mesh components would double-transform some geometry. It must also define explicit handling for unresolved node identities before reconciling instance overrides.

## Implementation continuation — 8 October 2026: immutable lossless hierarchy artifacts

Version 3 imports now store a separate .plutomodelhierarchy artifact in the immutable Library generation. Its bounded version 1 little-endian codec stores exact local matrices, selected scene roots, persistent or explicitly unresolved node identities, and baked/animated/skinned submesh binding provenance. World matrices are reconstructed with the iterative topology validator. Names and keys use bounded length framing; node/binding inventories and total bytes are capped before allocation. Cycles, invalid indices, duplicate or cross-kind identities, non-finite transforms, unsupported versions, trailing data and truncated input fail without replacing caller output.

The source package retains a small versioned MODEL_HIERARCHY descriptor containing virtual location and SHA-256, rather than copying hierarchy matrices into the authored sidecar. LoadModelHierarchyAsset resolves the source-selected Library generation, validates canonical containment and ordinary file status, hashes exactly the bytes decoded, and checks source ownership, active correspondence and mesh identity. Missing/corrupt Library preserves a previously decoded hierarchy in the caller. Unknown package records survive descriptor updates; duplicate/malformed/future descriptors reject replacement and block import reconciliation/database publication conservatively.

Recipe version 9 accounts for the new artifact. Cold imports stage it with native products; warm matching requires it; final immutable-generation validation verifies it. Its descriptor is distinct from native object baselines, so no extra public catalog object or runtime dependency is invented. Debounced watch inventories include the private artifact. Corruption requests safe reconstruction, and complete Library deletion reconstructs the same authored metadata and identities. Generated prefab entities, exact matrix support in scene transforms, animation/baking compensation and override reconciliation are still outstanding; this artifact supplies their lossless CPU input without prematurely changing scene behavior.

Validation: editor/runtime/CLI builds and all 19 pipeline regressions passed. Tests cover deterministic binary round trip with shear and negative scale, computed world transforms, selected roots, animated/skinned provenance, unresolved identities, all truncated prefixes, excessive counts, unsupported versions, trailing data, cycles, duplicate/cross-kind IDs, non-finite matrices, descriptor round trip/unknown preservation/future protection, source-selected Library loading, cache deletion, private hierarchy corruption, watch detection and reconstruction. An isolated M16 copy imported its hierarchy, kept only source plus sidecar in Assets, reused its cache and reported current. Original CoD files remain untouched.

## Existing-project review gate — 8 October 2026: CoD source history

The read-only inventory still reports a missing Soldier_Rig_Review.glb sidecar and an orphan grass_green/scene.gltf.plutometa duplicating the active SourceModels/scene/scene.gltf identity. Further read-only inspection confirms grass_green/grass_green.gltf exists with its own different identity. Both duplicate sidecars are byte-identical, but the active grass and scene glTF sources have different bytes. These facts cannot establish whether the missing grass_green/scene.gltf was intentionally renamed or is intended to be restored.

The user confirmed on 8 October 2026 that grass_green/scene.gltf was renamed to grass_green/grass_green.gltf. The source-history question is resolved; the conversion still requires verified backup, a reviewed dry run and transactional application. If it was renamed, the proposed conversion preserves both existing active IDs, backs up the orphan metadata before quarantining it, and maps old grass source references to the confirmed replacement using format-specific serializers. If restoration is intended, preserve the orphan and review which source retains the shared identity before assigning a replacement. Do not infer ownership from filenames, discard the orphan, rewrite IDs, or rewrite references based only on the current inventory. No CoD files have been modified. This is a project conversion review gate; other engine milestones remain in progress.

Reviewed IDs: active scene/orphan share e06503e4995519e444ff523bf896a57a; active grass uses 8f9d228395c118181cfabf4be45591d4. Duplicate sidecar SHA-256: 10b9131c0c0ded1eed88b925c4d39d065e5a2053a0ff587c8855c486230138ac. Active grass glTF SHA-256: 14414827e9793221e984f9b14c7c503a50ec9c61963bb8a5e5b874c426849417. Active scene glTF SHA-256: 1874336d6071c548480b5b62e424a905157ba1be45ff9035c6533c8f41e90e13. Revalidate all hashes under the project lock immediately before any future backed-up conversion.

## Confirmed source rename planning — 8 October 2026

The migration planner accepts explicit old/replacement project references through AssetMigrationOptions and the CLI --plan-migration --renamed option. It requires a missing old model, an ordinary existing replacement, and valid persisted sidecars for both. SHA-256 evidence binds the orphan metadata, replacement metadata and replacement source. Planning remains read-only, preserves all current IDs and publishes its report only on success. Invalid references, traversal, duplicate origins and changed inputs fail without replacing the caller’s report.

The raw audit is retained unchanged. A separate proposed-resolution inventory describes the orphan quarantine and duplicate-family resolution that would result after a future verified backup. Every duplicate family is evaluated together; removing one orphan cannot conceal two surviving owners. Physical references to the confirmed old location can propose the replacement identity. Logical references using a shared old identity require explicit occurrence review because the active scene legitimately owns that identity. Generated files with import-source provenance are marked for reimport rather than direct authored-file conversion.

Remaining implementation: create a checked backup manifest, acquire the project lock and revalidate all evidence, use format-specific writers for supported authored files, journal replacement/quarantine operations, verify the resulting catalog and runtime references, and provide rollback before enabling project conversion. No original CoD files have been changed.

Read-only validation after confirmation: editor/runtime/CLI built successfully and all 19 pipeline regressions passed (13.59 seconds). The original CoD dry run produced 2,274 possible occurrence mappings. Its raw duplicate/orphan issues have proposed resolutions bound to the reviewed hashes; the missing Soldier sidecar remains a warning. Two blocking entries are C# sound-directory prefixes in GameplaySounds.cs, used by concatenation and path replacement. These are semantic compatibility paths, not individual asset objects: do not assign them file identities or rewrite them as logical URIs. Future conversion must retain that behavior and establish explicit cook coverage for dynamically constructed sound paths. Full report: out/cod-asset-migration-plan-2026-10-08.txt.

## Format-aware material conversion preparation — 8 October 2026

PrepareMaterialReferenceMigration provides a CPU-only, in-memory preparation boundary for authored native .plutomaterial/.mat references. It requires the exact SHA-256 audited bytes, rejects imported/unresolved files, validates logical targets and unique occurrence lines, and changes only recognized texture, shader-graph and named shader-graph texture fields. Unknown fields, numeric precision, mixed line endings and missing final newline survive unchanged. Stale hashes, malformed/unsupported fields, duplicate or out-of-range occurrences, invalid logical targets and bounded-size failures leave prior caller output intact. No project bytes are published and this is not an apply operation.

The shared dependency scanner now includes named ShaderGraphTexture bindings; these were previously omitted. Regression coverage verifies conversion preservation, stale-input/imported/unsupported/out-of-range rejection and discovery of the named texture dependency. Editor/runtime/CLI builds and all 19 pipeline regressions passed after this change (13.45 seconds). Backups, journaled application, other format writers and rollback remain outstanding.

## Verified migration recovery copies — 8 October 2026

CreateAssetMigrationBackup accepts an explicit, bounded mutation set with expected SHA-256 values. Under the shared project writer lock it rejects pending import transactions, invalid/traversing paths, infrastructure inputs, non-ordinary source chains and stale hashes before copying. Windows ambiguous path components are rejected and canonical containment is checked. Each new recovery directory lives under .pluto-migration-backups, outside disposable Library, and is excluded from asset scan/watch/cook/move policy. Do not version-control recovery copies; retain them until conversion validation and rollback review have completed. This service does not claim to back up the whole project: the future conversion coordinator must include its project manifest, changed authored assets, identities, source settings and quarantine inputs.

Copies are hashed and sources are revalidated before a versioned manifest is sealed. VerifyAssetMigrationBackup performs a bounded manifest read, validates record inventories and relative paths, rejects incomplete/future/malformed manifests, verifies ordinary copied files and digests, and rechecks the manifest bytes. Failure preserves prior caller output; failed creation attempts remain separately inspectable and marked incomplete. The service never replaces original files and is not yet exposed as project conversion or rollback. Closed files and hash verification provide application-level integrity, not a guarantee against power loss without filesystem flushes. Any later apply or restore must reacquire the project lock and verify current expected hashes to avoid overwriting subsequent user edits.

Validation: editor/runtime/CLI builds passed; all 19 pipeline regressions passed in 12.84 seconds, including recovery-copy round trip, corruption, invalid paths, stale input, cancellation, incomplete/future manifests, writer-lock contention and preservation of original bytes. Refreshed original CoD read-only report still lists 2,274 possible mappings and the two dynamic C# directory prefixes requiring semantic/cook treatment. No original CoD files were written.

Backup sizing: read-only enumeration of the original CoD Assets tree found 2,287 files totaling 2,615,915,686 bytes (about 2.44 GiB), before the project manifest and backup records. No recovery copy has been created for the original project; only scratch tests exercised backup creation. A full conversion backup must budget for this content and retain it outside disposable Library.

## CoD conversion implementation — 8 October 2026

The user authorized conversion of the original CoD project. A verified complete
recovery copy of 5,823 project files was created under CoD's excluded
`.pluto-migration-backups/2026-10-08-1791468004973/Original`, with SHA-256 manifest.
Preparation and runtime validation first used an isolated short-path working
copy. Windows path length limits made the deeply nested initial test location
unsuitable for native animation paths; production paths retain the shorter
original CoD root.

All 19 model sources receive persistent import settings, existing identities
and explicit material remaps. Matching legacy object IDs are adopted using
actual CPU parser source keys rather than filename guesses. An optional strict
versioned MODEL_OUTPUT_DIRECTORY sidecar record places new virtual products
under ImportedModels/<source-id>, leaving the existing edited native gameplay
assets authored and intact. Library holds generated payloads. Parser inspection
and import share texture source-key logic, including color space.

Format-aware preparation changed 194 references in 25 scene/prefab/material
files, preserving other fields and formatting. Dynamic script directory paths
remain compatible. The confirmed orphan grass sidecar is quarantined; active
grass and scene IDs remain unchanged. Missing source metadata was added, project
version changed to 3, managed engine reference paths made configurable, current
managed assemblies rebuilt, and cache/recovery paths added to Git ignores.

Local publication tooling stages hash-verified replacements, acquires the
shared import writer lock, rejects pending transactions, revalidates originals
and journals every replacement/quarantine operation. Rollback verifies both
recovery bytes and expected current bytes, refusing subsequent user changes.
This is explicit project-specific conversion tooling, not a completed generic
migration coordinator or a claim of power-loss durability. The journal is
checkpointed after fresh original-root imports and preservation checks.

The runtime source-project smoke test exposed missing catalog initialization:
exported games already load a catalog, but direct source execution did not.
Startup now obtains a read-only AssetDatabase catalog/storage snapshot when no
exported catalog exists. It does not create metadata or implicitly import.
Both title and Main gameplay Vulkan smoke tests passed in the isolated copy,
and all 19 native asset pipeline regressions passed after the fix. The existing
managed gameplay test suite stops at a stale hardcoded scene entity ID in
HandSystemTests; original scene bytes show the same failure. Detailed project
workflow, validation and recovery notes are in CoD/Docs/AssetMigration-2026-10-08.md.

Earlier "no original CoD files modified" entries above describe their historical
checkpoints. They are superseded by this authorized conversion. Remaining broad
plan milestones include generated hierarchy prefab instantiation/overrides,
general importer coverage, cache collection/leases and reusable migration
coordination; CoD conversion does not mark those milestones complete.

Final original-project acceptance: all 19 fresh imports succeeded; all 19
reported current; the read-only audit reported 825 assets, 1,147 metadata files
and zero issues. Actual-root title and Main gameplay Vulkan smoke tests exited
successfully. A full non-pruned standalone export produced a 3,575,736,169-byte
pack; all 1,611 packed files verified and the packed title benchmark passed.
The final lock-protected recovery checkpoint records 1,181 project changes and
checks every original manifest entry against either its unchanged original hash
or its journaled replacement. Original source/native payloads outside the
explicit migration set are preserved. The recovery journal status is validated.
