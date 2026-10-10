#pragma once

#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include <optional>

namespace PlutoGE::assets
{
    // A prepared layout alone cannot prove which baked vertices its bindings use.
    // Capture these values from the same verified accepted import generation.
    struct StaticModelInstanceGeneration
    {
        StaticModelInstanceLayout layout;
        content::ContentDigest meshDigest{};
        std::size_t submeshCount = 0;
        std::size_t materialSlotCount = 0;
    };

    struct StaticModelNodeOverride
    {
        std::uint64_t sourceNodeId = 0;
        // Explicit authored values, including values equal to previous defaults.
        // An absent value inherits the next source default; this is not a delta.
        std::optional<glm::mat4> localTransform = std::nullopt;
        std::optional<bool> enabled = std::nullopt;
        // Added components/children and other edits also protect their subtree.
        bool hasAdditionalEdits = false;
        // Generated-node structural edits require review or explicit unpacking.
        bool hasStructuralEdits = false;
        // Authored state on generated geometry needs the accepted ordered binding
        // inventory; node-only edits can follow updated geometry safely.
        bool hasGeometryEdits = false;
        // Per-control authored intent; absent controls inherit incoming defaults.
        // localTransform remains the explicit full-affine override for old records.
        std::optional<glm::vec3> localPosition = std::nullopt;
        std::optional<glm::vec3> localRotation = std::nullopt;
        std::optional<glm::vec3> localScale = std::nullopt;
    };

    struct StaticModelMaterialOverride
    {
        // Address in the accepted generation only, never a persistent identity.
        std::size_t bindingIndex = 0;
        std::size_t materialSlot = 0;
        std::string reference; // Empty deliberately clears the material.
    };

    struct StaticModelInstanceOverrides
    {
        // Prevent applying edits captured from another baseline to this instance.
        content::ContentDigest hierarchyDigest{};
        content::ContentDigest meshDigest{};
        std::vector<StaticModelNodeOverride> nodes;
        std::vector<StaticModelMaterialOverride> materials;
    };

    enum class StaticModelInstanceConflictKind
    {
        SourceMeshChanged,
        EditedNodeRemoved,
        EditedSubtreeReparented,
        MaterialBindingChanged,
        InstanceStructureEdited,
        GeometryBindingChanged,
        OverrideDependencyRemoved
    };
    struct StaticModelInstanceConflict
    {
        StaticModelInstanceConflictKind kind;
        std::uint64_t sourceNodeId = 0;
    };
    struct ReconciledStaticModelNode
    {
        std::uint64_t sourceNodeId = 0;
        glm::mat4 localTransform{1.0f};
        bool enabled = true;
    };
    enum class StaticModelInstanceReconciliationStatus { Unprepared, Ready, Conflict };
    struct StaticModelInstanceReconciliation
    {
        // Conflicts retain the ENTIRE accepted generation and its overrides.
        // Never partially publish safe siblings into a conflicted instance.
        StaticModelInstanceReconciliationStatus status = StaticModelInstanceReconciliationStatus::Unprepared;
        StaticModelInstanceGeneration generation;
        StaticModelInstanceOverrides overrides;
        std::vector<ReconciledStaticModelNode> nodes; // Generation layout order.
        std::vector<StaticModelInstanceConflict> conflicts;
        bool CanPublish() const noexcept { return status == StaticModelInstanceReconciliationStatus::Ready && conflicts.empty(); }
    };

    // CPU-only three-way preparation: accepted source defaults, explicit instance
    // overrides, and newly imported defaults. No entities/resources/history/I/O.
    // Instance placement belongs to a separate scene root and is never reconciled.
    // Structural conflicts are successful, reviewable results holding old state;
    // invalid/stale evidence fails without replacing output. Callers must verify
    // artifact bytes and revalidate instance/generation evidence before publishing.
    bool PrepareStaticModelInstanceReconciliation(const StaticModelInstanceGeneration &accepted,
        const StaticModelInstanceOverrides &overrides, const StaticModelInstanceGeneration &incoming,
        StaticModelInstanceReconciliation &output, std::string *errorMessage = nullptr);
}
