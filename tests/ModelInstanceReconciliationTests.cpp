#include <PlutoGE/assets/ModelInstanceReconciliation.h>
#include <PlutoGE/math/AffineTransform.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::assets;
    constexpr std::uint64_t Root = (1ull << 40) + 1, Part = Root + 1, Empty = Root + 2, Other = Root + 3;
    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
    PlutoGE::content::ContentDigest Digest(unsigned char value)
    {
        PlutoGE::content::ContentDigest digest{}; digest[0] = value; return digest;
    }
    StaticModelInstanceGeneration Generation()
    {
        StaticModelInstanceGeneration result;
        result.layout.sourceAssetId = "source";
        result.layout.meshReference = "asset://source#7";
        result.layout.hierarchyDigest = Digest(1);
        result.meshDigest = Digest(2);
        result.submeshCount = 2; result.materialSlotCount = 2;
        result.layout.nodes = {{Root, "Root", -1, glm::mat4(1)}, {Part, "Part", 0, glm::mat4(1)},
            {Empty, "Empty", 1, glm::mat4(1)}, {Other, "Other", 0, glm::mat4(1)}};
        auto compensation = glm::mat4(1); compensation[1][0] = -0.25f;
        result.layout.bindings = {{1, 0, compensation}, {1, 1, glm::mat4(1)}, {3, 0, glm::mat4(1)}};
        return result;
    }
    StaticModelInstanceOverrides Overrides(const StaticModelInstanceGeneration &generation)
    {
        StaticModelInstanceOverrides result;
        result.hierarchyDigest = generation.layout.hierarchyDigest; result.meshDigest = generation.meshDigest;
        return result;
    }
    StaticModelInstanceReconciliation Prepare(const StaticModelInstanceGeneration &accepted,
        const StaticModelInstanceOverrides &overrides, const StaticModelInstanceGeneration &incoming)
    {
        StaticModelInstanceReconciliation result; std::string error;
        Require(PrepareStaticModelInstanceReconciliation(accepted, overrides, incoming, result, &error), error);
        return result;
    }
    bool Conflict(const StaticModelInstanceReconciliation &result, StaticModelInstanceConflictKind kind, std::uint64_t node)
    {
        return std::any_of(result.conflicts.begin(), result.conflicts.end(), [&](const auto &value)
            { return value.kind == kind && value.sourceNodeId == node; });
    }
    void PropertyInheritance()
    {
        const auto accepted = Generation(); auto incoming = accepted; auto edits = Overrides(accepted);
        incoming.layout.hierarchyDigest = Digest(3);
        incoming.layout.nodes[1].localTransform[3][0] = 10;
        incoming.layout.nodes[2].localTransform[1][0] = 0.75f;
        incoming.layout.nodes[2].localTransform[0][0] = -2;
        incoming.layout.nodes[2].name = "Renamed by producer ID";
        // Explicit equal-to-old-default overrides remain overrides after reimport.
        edits.nodes = {{Part, glm::mat4(1), false}, {Other, std::nullopt, true}};
        const auto result = Prepare(accepted, edits, incoming);
        Require(result.CanPublish() && result.nodes[1].localTransform == glm::mat4(1) && !result.nodes[1].enabled,
            "Explicit transform/enabled overrides did not win over source defaults");
        Require(result.nodes[2].sourceNodeId == Empty && result.nodes[2].localTransform == incoming.layout.nodes[2].localTransform &&
            result.generation.layout.nodes[2].name == incoming.layout.nodes[2].name, "Unedited affine defaults or source names did not update");
        Require(result.overrides.nodes.size() == 2 && result.overrides.hierarchyDigest == incoming.layout.hierarchyDigest,
            "Overrides did not advance to the prepared baseline");
        Require(accepted.layout.nodes[1].localTransform == glm::mat4(1) && edits.hierarchyDigest == accepted.layout.hierarchyDigest,
            "Preparation mutated caller inputs");
        incoming.layout.nodes.push_back({Other + 1, "Added", 1, glm::mat4(1)});
        const auto added = Prepare(accepted, edits, incoming);
        Require(added.CanPublish() && added.nodes.back().sourceNodeId == Other + 1 && added.nodes.back().enabled,
            "New source nodes did not inherit generated defaults");
        incoming.meshDigest = Digest(4);
        incoming.layout.bindings[0].geometryToNode[1][0] = -0.5f;
        const auto geometry = Prepare(accepted, edits, incoming);
        Require(geometry.CanPublish() && geometry.generation.meshDigest == incoming.meshDigest &&
            geometry.generation.layout.bindings[0].geometryToNode == incoming.layout.bindings[0].geometryToNode,
            "Unedited geometry did not advance with its compensation");
    }
    void GranularTransformInheritance()
    {
        const auto accepted = Generation();
        auto incoming = accepted;
        incoming.layout.hierarchyDigest = Digest(3);
        auto correction = glm::mat4(1); correction[1][0] = .35f;
        incoming.layout.nodes[1].localTransform = PlutoGE::math::ComposeLocalTransform(
            {{10,20,30},{15,35,55},{-2,3,4}}, correction);
        auto edits = Overrides(accepted);
        StaticModelNodeOverride authored;
        authored.sourceNodeId = Part;
        authored.localPosition = glm::vec3(0); // Explicitly equal to accepted default.
        edits.nodes.push_back(authored);
        auto result = Prepare(accepted, edits, incoming);
        auto expected = incoming.layout.nodes[1].localTransform; expected[3] = glm::vec4(0,0,0,1);
        Require(result.CanPublish() && result.nodes[1].localTransform == expected &&
            result.overrides.nodes[0].localPosition == glm::vec3(0) && !result.overrides.nodes[0].localTransform,
            "Position intent froze incoming rotation, reflection, scale or shear");
        edits.nodes[0].localRotation = glm::vec3(0,45,0);
        PlutoGE::math::ResolvedTransform resolved;
        Require(PlutoGE::math::ResolveLocalTransform(incoming.layout.nodes[1].localTransform,
            edits.nodes[0].localPosition, edits.nodes[0].localRotation, std::nullopt, resolved), "Expected transform failed");
        result = Prepare(accepted, edits, incoming);
        Require(result.CanPublish() && result.nodes[1].localTransform == resolved.matrix,
            "Authored rotation discarded incoming scale or affine correction");
        edits.nodes[0].localScale = glm::vec3(0,2,-3);
        result = Prepare(accepted, edits, incoming);
        Require(result.CanPublish() && std::abs(glm::determinant(result.nodes[1].localTransform)) < .0001f,
            "Finite authored zero scale was rejected");
        auto removed = incoming;
        removed.layout.nodes.erase(removed.layout.nodes.begin() + 1, removed.layout.nodes.begin() + 3);
        removed.layout.bindings = {{1,0,glm::mat4(1)}};
        Require(Conflict(Prepare(accepted, edits, removed), StaticModelInstanceConflictKind::EditedNodeRemoved, Part),
            "Per-control authored intent did not protect a removed node");
        edits.nodes[0].localScale->x = std::numeric_limits<float>::quiet_NaN();
        StaticModelInstanceReconciliation unchanged; unchanged.generation.meshDigest = Digest(99);
        std::string error;
        Require(!PrepareStaticModelInstanceReconciliation(accepted, edits, incoming, unchanged, &error) &&
            unchanged.generation.meshDigest == Digest(99), "Invalid transform controls replaced prepared output");
    }

    void StructuralConflicts()
    {
        const auto accepted = Generation(); auto incoming = accepted; auto edits = Overrides(accepted);
        incoming.layout.hierarchyDigest = Digest(3);
        incoming.layout.nodes.erase(incoming.layout.nodes.begin() + 2);
        incoming.layout.bindings[2].nodeIndex = 2;
        auto result = Prepare(accepted, edits, incoming);
        Require(result.CanPublish() && result.nodes.size() == 3, "Unedited source deletion was blocked");
        edits.nodes = {{Empty, std::nullopt, false}};
        result = Prepare(accepted, edits, incoming);
        Require(!result.CanPublish() && Conflict(result, StaticModelInstanceConflictKind::EditedNodeRemoved, Empty) &&
            result.generation.layout.hierarchyDigest == accepted.layout.hierarchyDigest && result.nodes.size() == 4 &&
            !result.nodes[2].enabled, "Edited deletion did not retain the whole accepted instance");
        incoming = accepted; incoming.layout.hierarchyDigest = Digest(3); incoming.layout.nodes[1].parentIndex = -1;
        incoming.layout.nodes[3].localTransform[3][0] = 50;
        result = Prepare(accepted, edits, incoming);
        Require(Conflict(result, StaticModelInstanceConflictKind::EditedSubtreeReparented, Part) &&
            result.nodes[3].localTransform == accepted.layout.nodes[3].localTransform,
            "Edited descendant did not protect reparented ancestor or safe sibling was partially updated");
        auto materialEdits = Overrides(accepted);
        materialEdits.materials = {{0, 0, "project://Authored.plutomaterial"}};
        Require(Conflict(Prepare(accepted, materialEdits, incoming), StaticModelInstanceConflictKind::EditedSubtreeReparented, Part),
            "Material overrides did not protect reparented ancestors");
        edits = Overrides(accepted);
        Require(Prepare(accepted, edits, incoming).CanPublish(), "Unedited reparent was blocked");
        edits.nodes = {{Part, std::nullopt, std::nullopt, true}};
        Require(!Prepare(accepted, edits, incoming).CanPublish(), "Additional scene edits did not protect the subtree");
        edits.nodes = {{Part, std::nullopt, std::nullopt, false, true}};
        Require(Conflict(Prepare(accepted, edits, accepted), StaticModelInstanceConflictKind::InstanceStructureEdited, Part),
            "Unsupported authored structural edits were silently accepted");
        incoming = accepted; incoming.layout.meshReference = "asset://source#8";
        Require(Conflict(Prepare(accepted, Overrides(accepted), incoming), StaticModelInstanceConflictKind::SourceMeshChanged, 0),
            "Changed source mesh identity was guessed");
    }
    void MaterialBindings()
    {
        const auto accepted = Generation(); auto incoming = accepted; auto edits = Overrides(accepted);
        edits.materials = {{0, 0, "asset://authored-material#0"}, {1, 1, ""}};
        // Reorder source nodes and global bindings, preserving each node's ordered
        // binding inventory. Neither snapshot index is a persistent identity.
        incoming.layout.hierarchyDigest = Digest(3);
        incoming.layout.nodes = {accepted.layout.nodes[0], accepted.layout.nodes[3], accepted.layout.nodes[1], accepted.layout.nodes[2]};
        incoming.layout.nodes[3].parentIndex = 2;
        incoming.layout.bindings = {accepted.layout.bindings[2], accepted.layout.bindings[0], accepted.layout.bindings[1]};
        incoming.layout.bindings[0].nodeIndex = 1;
        incoming.layout.bindings[1].nodeIndex = incoming.layout.bindings[2].nodeIndex = 2;
        auto result = Prepare(accepted, edits, incoming);
        Require(result.CanPublish() && result.overrides.materials[0].bindingIndex == 1 &&
            result.overrides.materials[1].bindingIndex == 2 && result.overrides.materials[1].reference.empty(),
            "Unchanged node binding inventory did not rebase generation-scoped material addresses");
        incoming.meshDigest = Digest(9);
        result = Prepare(accepted, edits, incoming);
        Require(!result.CanPublish() && result.conflicts.size() == 1 &&
            Conflict(result, StaticModelInstanceConflictKind::MaterialBindingChanged, Part) &&
            result.overrides.materials[0].bindingIndex == 0 && result.generation.meshDigest == accepted.meshDigest,
            "Changed geometry carried material overrides by stale submesh indices or lost accepted state");
        incoming = accepted; incoming.layout.hierarchyDigest = Digest(3);
        std::swap(incoming.layout.bindings[0], incoming.layout.bindings[1]);
        Require(!Prepare(accepted, edits, incoming).CanPublish(), "Reordered per-node bindings guessed material correspondence");
        incoming = accepted; incoming.layout.hierarchyDigest = Digest(3);
        incoming.layout.bindings[0].geometryToNode[3][0] = 1;
        Require(!Prepare(accepted, edits, incoming).CanPublish(), "Changed compensation retained generation-scoped material edits");
    }
    void GeometryEdits()
    {
        const auto accepted = Generation(); auto incoming = accepted; auto edits = Overrides(accepted);
        incoming.layout.hierarchyDigest = Digest(11); incoming.meshDigest = Digest(12);
        incoming.layout.nodes[3].localTransform[3][0] = 25;
        edits.nodes = {{Part, std::nullopt, std::nullopt, true, false, false}};
        Require(Prepare(accepted, edits, incoming).CanPublish(), "Node-only additional edits blocked geometry inheritance");
        edits.nodes.front().hasGeometryEdits = true;
        auto result = Prepare(accepted, edits, incoming);
        Require(!result.CanPublish() && Conflict(result, StaticModelInstanceConflictKind::GeometryBindingChanged, Part) &&
            result.generation.meshDigest == accepted.meshDigest && result.nodes[3].localTransform == accepted.layout.nodes[3].localTransform,
            "Edited geometry advanced to new mesh bytes or partially updated a safe sibling");
        incoming = accepted; incoming.layout.hierarchyDigest = Digest(11);
        incoming.layout.bindings[0].geometryToNode[3][0] = 2;
        Require(Conflict(Prepare(accepted, edits, incoming), StaticModelInstanceConflictKind::GeometryBindingChanged, Part),
            "Changed binding compensation carried authored geometry state");
        incoming = accepted; incoming.layout.hierarchyDigest = Digest(11);
        incoming.layout.bindings.erase(incoming.layout.bindings.begin());
        Require(!Prepare(accepted, edits, incoming).CanPublish(), "Removed generated binding discarded authored geometry state");
        incoming = accepted; incoming.layout.hierarchyDigest = Digest(11);
        incoming.layout.nodes[3].name = "Renamed sibling";
        Require(Prepare(accepted, edits, incoming).CanPublish(), "Unchanged geometry binding blocked safe source metadata changes");
    }
    void InvalidEvidence()
    {
        const auto accepted = Generation(); const auto edits = Overrides(accepted);
        auto output = Prepare(accepted, edits, accepted);
        output.generation.layout.sourceAssetId = "sentinel";
        const auto reject = [&](const StaticModelInstanceGeneration &old, const StaticModelInstanceOverrides &overrides,
            const StaticModelInstanceGeneration &next)
        {
            std::string error;
            Require(!PrepareStaticModelInstanceReconciliation(old, overrides, next, output, &error) && !error.empty() &&
                output.generation.layout.sourceAssetId == "sentinel", "Invalid preparation replaced prior output");
        };
        auto bad = accepted; bad.meshDigest = {}; reject(accepted, edits, bad);
        bad = accepted; bad.layout.sourceAssetId = "different"; reject(accepted, edits, bad);
        bad = accepted; bad.layout.nodes[1].sourceNodeId = Root; reject(accepted, edits, bad);
        bad = accepted; bad.layout.nodes[1].parentIndex = 2; reject(accepted, edits, bad);
        bad = accepted; bad.layout.nodes[1].localTransform[0][0] = 0; reject(accepted, edits, bad);
        bad = accepted; bad.layout.bindings[0].submeshIndex = 2; reject(accepted, edits, bad);
        bad = accepted; bad.layout.nodes[1].localTransform[3][0] = 1; reject(accepted, edits, bad);
        bad = accepted; ++bad.submeshCount; reject(accepted, edits, bad);
        auto invalid = edits; invalid.hierarchyDigest = Digest(99); reject(accepted, invalid, accepted);
        invalid = edits; invalid.nodes = {{Part, glm::mat4(1)}, {Part, glm::mat4(1)}}; reject(accepted, invalid, accepted);
        invalid = edits; invalid.nodes = {{99, glm::mat4(1)}}; reject(accepted, invalid, accepted);
        invalid = edits; invalid.nodes = {{Part, glm::mat4(1)}};
        (*invalid.nodes[0].localTransform)[0][0] = std::numeric_limits<float>::quiet_NaN(); reject(accepted, invalid, accepted);
        invalid = edits; invalid.materials = {{99, 0, ""}}; reject(accepted, invalid, accepted);
        invalid = edits; invalid.materials = {{0, 2, ""}}; reject(accepted, invalid, accepted);
        invalid = edits; invalid.materials = {{0, 0, "asset://broken#bad"}}; reject(accepted, invalid, accepted);
        invalid = edits; invalid.materials = {{0, 0, ""}, {0, 0, ""}}; reject(accepted, invalid, accepted);
        bad = accepted; bad.layout.hierarchyDigest = Digest(7); bad.layout.nodes[1].localTransform[0][3] = 1;
        reject(accepted, edits, bad);
        bad = accepted; bad.layout.hierarchyDigest = Digest(7); bad.layout.bindings[0].geometryToNode[0][0] = 0;
        reject(accepted, edits, bad);
        bad = accepted; bad.layout.hierarchyDigest = Digest(7); bad.layout.nodes[1].localTransform[0][0] = 1e-9f;
        Require(Prepare(accepted, edits, bad).CanPublish(), "Small nonsingular affine scale was rejected");
        auto deep = accepted; deep.layout.hierarchyDigest = Digest(8); deep.layout.bindings.clear(); deep.layout.nodes.clear();
        for (int index = 0; index < 128; ++index)
            deep.layout.nodes.push_back({Root + static_cast<std::uint64_t>(index), "Node", index - 1, glm::mat4(1)});
        Require(Prepare(deep, Overrides(deep), deep).CanPublish(), "Supported source depth was rejected");
        deep.layout.nodes.push_back({Root + 128, "Too deep", 127, glm::mat4(1)});
        reject(deep, Overrides(deep), deep);
        // Empty scenes and explicit empty records are well-defined.
        auto empty = accepted; empty.layout.hierarchyDigest = Digest(4); empty.layout.nodes.clear(); empty.layout.bindings.clear();
        Require(Prepare(accepted, edits, empty).CanPublish(), "Unedited empty selected scene was rejected");
        invalid = edits; invalid.nodes = {{Empty}};
        Require(Prepare(accepted, invalid, empty).overrides.nodes.empty(), "Empty override records prevented unedited deletion");
    }
}
int main()
{
    try
    {
        Require(!StaticModelInstanceReconciliation{}.CanPublish(), "Unprepared reconciliation appeared publishable");
        PropertyInheritance(); GranularTransformInheritance(); StructuralConflicts(); MaterialBindings(); GeometryEdits(); InvalidEvidence();
        std::cout << "Static model instance reconciliation tests passed\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
