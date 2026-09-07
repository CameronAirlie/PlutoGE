# Prefab variants (M10)

A prefab variant is a `.plutoprefab` asset with a base reference and explicit
property overrides. It inherits the base hierarchy and components each time it is
resolved. A variant may derive from another variant, up to 64 levels. Ordinary
scene-format prefab files continue to load through the same API.

## Editor workflow

1. Instantiate a prefab and edit its name, tags, active state, transform, or
   component properties. The Inspector records explicit override paths.
2. On the instance root, choose **Create Variant**, enter a name, and click
   **Create**. The new asset is saved in `Assets/Prefabs`; the selected instance
   becomes an instance of that variant. Existing files are not overwritten.
3. **Apply To Prefab** on a variant instance merges its explicit overrides into
   the variant asset. The base reference remains intact; the base asset is not
   overwritten. Existing variant overrides remain explicit even if they happen
   to equal their current inherited value.
4. **Update From Prefab** resolves the latest base chain while retaining local
   instance overrides. Applying a base prefab also refreshes loaded instances of
   its derived variants.
5. **Revert Instance Overrides** discards local instance overrides and restores
   the resolved prefab/variant. Scene undo restores the previous instance state.

Asset creation and Apply write shared files. Scene undo covers the scene changes,
not those shared files. External asset edits propagate on the next instance update
or instantiation; live scene instances are not polled and replaced automatically.

## Scope and failure behavior

This delivery supports property overrides and nested **variant base chains**.
Hierarchy edits, component additions/removals, and overrides of repeated component
types are rejected when saving a variant. Make structural changes in the base,
then update the instance before applying. Separate nested prefab instances retain
the existing engine behavior; this does not introduce a nested-prefab authoring
system.

Cycles, missing bases, unsupported records, and overrides targeting deleted base
entities/properties fail resolution with an error. Failed dependency resolution
leaves an existing instance untouched. Variant writes use a temporary file and
replace the destination only after serialization succeeds.

The parsed-prefab cache tracks every base dependency using modification times and
content hashes, including rapid edits that share a timestamp. This avoids reusing
an old resolved variant when any ancestor changes. Ready checks read dependency
files to verify their hashes, but do not deserialize them again.

## Native API and cooking

- `Prefab::SaveVariant(instance, path, &error)` creates a derived asset or applies
  overrides to the instance's existing variant asset.
- `Prefab::GetVariantBase(reference)` returns its immediate base, or an empty string
  for ordinary/unreadable prefab files.
- `Prefab::RevertInstance(instance, &error)` restores inherited values.
- Existing Instantiate, Preload, IsReady, UpdateInstance, and UpdateInstances APIs
  resolve variants. Managed prefab instantiation therefore uses the same resolver.

Use `project://` base references for portable assets. Dependency scanning and pruned
cooking include variant bases and asset-valued overrides transitively. Project
validation recognizes the variant format and diagnoses missing references, malformed
records, and cyclic base chains. Runtime resolution additionally checks that override
targets still exist.

The regression suite exercises multi-level inheritance, explicit overrides, base
propagation, apply/revert, snapshot restoration, cache invalidation, diagnostics,
structural-edit rejection, and cooked loading. Interactive editor checks remain
pending.
