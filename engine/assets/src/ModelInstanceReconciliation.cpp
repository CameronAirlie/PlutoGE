#include "PlutoGE/assets/ModelInstanceReconciliation.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/math/AffineTransform.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::size_t MaxNodesAndBindings = 4096;
        using NodeIndex = std::map<std::uint64_t, std::size_t>;

        bool HasDigest(const content::ContentDigest &digest)
        {
            return std::any_of(digest.begin(), digest.end(), [](auto value) { return value != 0; });
        }

        bool Finite(const glm::mat4 &matrix)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row])) return false;
            return true;
        }

        bool ValidAffine(const glm::mat4 &matrix)
        {
            if (!Finite(matrix) || matrix[0][3] != 0 || matrix[1][3] != 0 ||
                matrix[2][3] != 0 || matrix[3][3] != 1) return false;
            const auto inverse = glm::inverse(matrix);
            if (!Finite(inverse)) return false;
            const auto residual = matrix * inverse;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(residual[column][row]) ||
                        std::abs(residual[column][row] - (column == row ? 1.0f : 0.0f)) > 0.001f) return false;
            return true;
        }

        NodeIndex Validate(const StaticModelInstanceGeneration &generation)
        {
            const auto &layout = generation.layout;
            std::string source;
            AssetReference mesh;
            if (layout.sourceAssetId.empty() || !SerializeAssetReference({layout.sourceAssetId, 0}, source) ||
                !ParseAssetReference(layout.meshReference, mesh) || mesh.assetId != layout.sourceAssetId || !mesh.localObjectId ||
                !HasDigest(layout.hierarchyDigest) || !HasDigest(generation.meshDigest))
                throw std::runtime_error("Missing or invalid source/mesh generation evidence.");
            if (layout.nodes.size() > MaxNodesAndBindings ||
                layout.bindings.size() > MaxNodesAndBindings - layout.nodes.size() ||
                generation.submeshCount > 100000 || generation.materialSlotCount > 100000)
                throw std::runtime_error("Static model generation exceeds its inventory limits.");
            NodeIndex nodes;
            std::vector<unsigned> depths;
            std::size_t nameBytes = 0;
            for (std::size_t index = 0; index < layout.nodes.size(); ++index)
            {
                const auto &node = layout.nodes[index];
                if (!node.sourceNodeId || node.sourceNodeId == mesh.localObjectId ||
                    !nodes.emplace(node.sourceNodeId, index).second || node.parentIndex < -1 ||
                    (node.parentIndex >= 0 && static_cast<std::size_t>(node.parentIndex) >= index) ||
                    node.name.size() > 1024 * 1024 || !ValidAffine(node.localTransform))
                    throw std::runtime_error("Invalid static model node identity, topology or affine transform.");
                if (node.name.size() > 32 * 1024 * 1024 - nameBytes)
                    throw std::runtime_error("Static model node names exceed their aggregate size limit.");
                nameBytes += node.name.size();
                const auto depth = node.parentIndex < 0 ? 1u : depths[node.parentIndex] + 1;
                if (depth > 128) throw std::runtime_error("Static model generation exceeds its depth limit.");
                depths.push_back(depth);
            }
            for (const auto &binding : layout.bindings)
                if (binding.nodeIndex < 0 || static_cast<std::size_t>(binding.nodeIndex) >= layout.nodes.size() ||
                    binding.submeshIndex >= generation.submeshCount || !ValidAffine(binding.geometryToNode))
                    throw std::runtime_error("Invalid static model geometry binding.");
            return nodes;
        }

        std::uint64_t Parent(const StaticModelInstanceLayout &layout, std::size_t index)
        {
            const auto parent = layout.nodes[index].parentIndex;
            return parent < 0 ? 0 : layout.nodes[parent].sourceNodeId;
        }

        bool SameLayout(const StaticModelInstanceLayout &first, const StaticModelInstanceLayout &second)
        {
            if (first.nodes.size() != second.nodes.size() || first.bindings.size() != second.bindings.size()) return false;
            for (std::size_t index = 0; index < first.nodes.size(); ++index)
            {
                const auto &a = first.nodes[index], &b = second.nodes[index];
                if (a.sourceNodeId != b.sourceNodeId || a.parentIndex != b.parentIndex ||
                    a.name != b.name || a.localTransform != b.localTransform) return false;
            }
            for (std::size_t index = 0; index < first.bindings.size(); ++index)
            {
                const auto &a = first.bindings[index], &b = second.bindings[index];
                if (a.nodeIndex != b.nodeIndex || a.submeshIndex != b.submeshIndex || a.geometryToNode != b.geometryToNode) return false;
            }
            return true;
        }

        using NodeBindings = std::map<std::uint64_t, std::vector<std::size_t>>;
        NodeBindings GroupBindings(const StaticModelInstanceLayout &layout)
        {
            NodeBindings result;
            for (std::size_t index = 0; index < layout.bindings.size(); ++index)
                result[layout.nodes[layout.bindings[index].nodeIndex].sourceNodeId].push_back(index);
            return result;
        }
    }

    bool PrepareStaticModelInstanceReconciliation(const StaticModelInstanceGeneration &accepted,
        const StaticModelInstanceOverrides &overrides, const StaticModelInstanceGeneration &incoming,
        StaticModelInstanceReconciliation &output, std::string *errorMessage)
    {
        try
        {
            const auto previousNodes = Validate(accepted), nextNodes = Validate(incoming);
            const auto &oldLayout = accepted.layout, &newLayout = incoming.layout;
            if (oldLayout.sourceAssetId != newLayout.sourceAssetId ||
                overrides.hierarchyDigest != oldLayout.hierarchyDigest || overrides.meshDigest != accepted.meshDigest)
                throw std::runtime_error("Instance overrides do not belong to the accepted source generation.");
            if (oldLayout.hierarchyDigest == newLayout.hierarchyDigest && !SameLayout(oldLayout, newLayout))
                throw std::runtime_error("Equal hierarchy evidence describes different layouts.");
            if (accepted.meshDigest == incoming.meshDigest &&
                (accepted.submeshCount != incoming.submeshCount || accepted.materialSlotCount != incoming.materialSlotCount))
                throw std::runtime_error("Equal mesh evidence describes different inventories.");
            if (overrides.nodes.size() > oldLayout.nodes.size() || overrides.materials.size() > MaxNodesAndBindings)
                throw std::runtime_error("Instance override inventory exceeds its limits.");
            std::map<std::uint64_t, const StaticModelNodeOverride *> nodeOverrides;
            std::vector<bool> editedSubtrees(oldLayout.nodes.size(), false);
            StaticModelInstanceReconciliation candidate;
            const auto conflict = [&](StaticModelInstanceConflictKind kind, std::uint64_t node)
            {
                candidate.conflicts.push_back({kind, node});
            };
            AssetReference oldMesh, newMesh;
            ParseAssetReference(oldLayout.meshReference, oldMesh);
            ParseAssetReference(newLayout.meshReference, newMesh);
            if (oldMesh != newMesh) conflict(StaticModelInstanceConflictKind::SourceMeshChanged, 0);
            const auto oldBindings = GroupBindings(oldLayout), newBindings = GroupBindings(newLayout);
            const auto sameGeometryBindings = [&](std::uint64_t node)
            {
                if (accepted.meshDigest != incoming.meshDigest) return false;
                const auto before = oldBindings.find(node), after = newBindings.find(node);
                if (before == oldBindings.end() || after == newBindings.end()) return before == oldBindings.end() && after == newBindings.end();
                if (before->second.size() != after->second.size()) return false;
                for (std::size_t ordinal = 0; ordinal < before->second.size(); ++ordinal)
                {
                    const auto &a = oldLayout.bindings[before->second[ordinal]], &b = newLayout.bindings[after->second[ordinal]];
                    if (a.submeshIndex != b.submeshIndex || a.geometryToNode != b.geometryToNode) return false;
                }
                return true;
            };
            for (const auto &value : overrides.nodes)
            {
                const auto found = previousNodes.find(value.sourceNodeId);
                if (found == previousNodes.end() || !nodeOverrides.emplace(value.sourceNodeId, &value).second ||
                    (value.localTransform && !ValidAffine(*value.localTransform)))
                    throw std::runtime_error("Unknown, duplicate or invalid instance node override.");
                math::ResolvedTransform checked;
                if (!math::ResolveLocalTransform(value.localTransform.value_or(oldLayout.nodes[found->second].localTransform),
                    value.localPosition, value.localRotation, value.localScale, checked))
                    throw std::runtime_error("Invalid authored transform controls.");
                editedSubtrees[found->second] = value.localTransform.has_value() || value.localPosition.has_value() ||
                    value.localRotation.has_value() || value.localScale.has_value() || value.enabled.has_value() ||
                    value.hasAdditionalEdits || value.hasStructuralEdits || value.hasGeometryEdits;
                if (value.hasStructuralEdits) conflict(StaticModelInstanceConflictKind::InstanceStructureEdited, value.sourceNodeId);
                if (value.hasGeometryEdits && !sameGeometryBindings(value.sourceNodeId))
                    conflict(StaticModelInstanceConflictKind::GeometryBindingChanged, value.sourceNodeId);
            }
            std::set<std::pair<std::size_t, std::size_t>> materialSlots;
            for (const auto &material : overrides.materials)
            {
                if (material.bindingIndex >= oldLayout.bindings.size() || material.materialSlot >= accepted.materialSlotCount ||
                    !materialSlots.emplace(material.bindingIndex, material.materialSlot).second ||
                    (!material.reference.empty() && NormalizeAssetReference(material.reference).empty()))
                    throw std::runtime_error("Unknown, duplicate or invalid instance material override.");
                editedSubtrees[oldLayout.bindings[material.bindingIndex].nodeIndex] = true;
            }
            for (std::size_t index = oldLayout.nodes.size(); index-- > 0;)
                if (editedSubtrees[index] && oldLayout.nodes[index].parentIndex >= 0)
                    editedSubtrees[oldLayout.nodes[index].parentIndex] = true;
            for (std::size_t index = 0; index < oldLayout.nodes.size(); ++index)
            {
                if (!editedSubtrees[index]) continue;
                const auto node = oldLayout.nodes[index].sourceNodeId;
                const auto next = nextNodes.find(node);
                if (next == nextNodes.end()) conflict(StaticModelInstanceConflictKind::EditedNodeRemoved, node);
                else if (Parent(oldLayout, index) != Parent(newLayout, next->second))
                    conflict(StaticModelInstanceConflictKind::EditedSubtreeReparented, node);
            }

            // Bindings have no persistent identity yet. Rebase their snapshot
            // addresses only when mesh bytes and the node's entire ordered binding
            // inventory are identical. Never guess correspondence by mesh index.
            auto rebased = overrides;

            for (auto &material : rebased.materials)
            {
                const auto &oldBinding = oldLayout.bindings[material.bindingIndex];
                const auto node = oldLayout.nodes[oldBinding.nodeIndex].sourceNodeId;
                const auto &before = oldBindings.at(node);
                const auto found = newBindings.find(node);
                bool same = accepted.meshDigest == incoming.meshDigest && found != newBindings.end() &&
                    before.size() == found->second.size();
                if (same)
                    for (std::size_t ordinal = 0; ordinal < before.size(); ++ordinal)
                    {
                        const auto &a = oldLayout.bindings[before[ordinal]], &b = newLayout.bindings[found->second[ordinal]];
                        if (a.submeshIndex != b.submeshIndex || a.geometryToNode != b.geometryToNode) { same = false; break; }
                    }
                if (!same) conflict(StaticModelInstanceConflictKind::MaterialBindingChanged, node);
                else
                {
                    const auto ordinal = std::find(before.begin(), before.end(), material.bindingIndex) - before.begin();
                    material.bindingIndex = found->second[ordinal];
                }
            }
            std::sort(candidate.conflicts.begin(), candidate.conflicts.end(), [](const auto &a, const auto &b)
            {
                return a.sourceNodeId != b.sourceNodeId ? a.sourceNodeId < b.sourceNodeId : a.kind < b.kind;
            });
            candidate.conflicts.erase(std::unique(candidate.conflicts.begin(), candidate.conflicts.end(), [](const auto &a, const auto &b)
                { return a.sourceNodeId == b.sourceNodeId && a.kind == b.kind; }), candidate.conflicts.end());
            candidate.status = candidate.conflicts.empty() ? StaticModelInstanceReconciliationStatus::Ready
                : StaticModelInstanceReconciliationStatus::Conflict;
            if (candidate.CanPublish())
            {
                candidate.generation = incoming;
                candidate.overrides = std::move(rebased);
                candidate.overrides.hierarchyDigest = newLayout.hierarchyDigest;
                candidate.overrides.meshDigest = incoming.meshDigest;
                // Empty explicit records are not edits and may disappear with
                // unedited removed nodes. Real edits would have produced conflicts.
                std::erase_if(candidate.overrides.nodes, [&](const auto &value) { return !nextNodes.contains(value.sourceNodeId); });
            }
            else
            {
                candidate.generation = accepted;
                candidate.overrides = overrides;
            }
            for (const auto &node : candidate.generation.layout.nodes)
            {
                ReconciledStaticModelNode value{node.sourceNodeId, node.localTransform, true};
                if (const auto found = nodeOverrides.find(node.sourceNodeId); found != nodeOverrides.end())
                {
                    const auto &authored = *found->second;
                    math::ResolvedTransform resolved;
                    if (!math::ResolveLocalTransform(authored.localTransform.value_or(node.localTransform),
                        authored.localPosition, authored.localRotation, authored.localScale, resolved))
                        throw std::runtime_error("Cannot resolve authored transform controls.");
                    value.localTransform = resolved.matrix;
                    if (found->second->enabled) value.enabled = *found->second->enabled;
                }
                candidate.nodes.push_back(value);
            }
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = std::string("Cannot prepare model instance reconciliation: ") + error.what();
            return false;
        }
    }
}
