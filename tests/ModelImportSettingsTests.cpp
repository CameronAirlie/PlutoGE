#include "PlutoGE/assets/ModelImportSettings.h"
#include "PlutoGE/assets/ModelNodeCorrespondence.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

int main()
{
    using namespace PlutoGE::assets;
    try
    {
        AssetMetadata metadata{.id = "owner", .extensionRecords = {"CUSTOM\tkeep", "MODEL_FUTURE\topaque"}};
        ModelImportSettings settings;
        std::string error;
        Require(ReadModelImportSettings(metadata, settings, &error) == ModelImportSettingsStatus::Missing, "Missing settings not distinguished");
        const auto mesh = ResolveModelObjectId(settings, "mesh/main", 42, &error);
        const auto material = ResolveModelObjectId(settings, "material/slot/0", 43, &error);
        Require(mesh == 42 && material == 43, "Legacy IDs were not adopted");
        settings.materialRemaps.push_back({material, {"authored", 0}, {}});
        Require(WriteModelImportSettings(metadata, settings, &error), "Settings serialization failed");
        Require(metadata.extensionRecords[0] == "CUSTOM\tkeep" && metadata.extensionRecords[1] == "MODEL_FUTURE\topaque", "Unknown settings records lost");
        ModelImportSettings loaded;
        Require(ReadModelImportSettings(metadata, loaded, &error) == ModelImportSettingsStatus::Success, "Settings round trip failed");
        BeginModelObjectImport(loaded);
        Require(ResolveModelObjectId(loaded, "mesh/main", 0, &error) == mesh, "Existing key changed identity");
        const auto fresh = ResolveModelObjectId(loaded, "mesh/new", 0, &error);
        Require(fresh != 0 && fresh != mesh && fresh != material, "Retired identity reused");
        Require(std::any_of(loaded.objects.begin(), loaded.objects.end(), [&](const auto &object) { return object.localId == material && object.retired; }),
                "Removed object was not retained as retired");
        Require(ResolveModelObjectId(loaded, "another-key", material, &error) == 0, "Ambiguous legacy ID reused");
        for (unsigned flags = 0; flags < 8; ++flags)
        {
            loaded.meshOptions = {(flags & 1u) != 0, (flags & 2u) != 0, (flags & 4u) != 0};
            Require(WriteModelImportSettings(metadata, loaded, &error), "Option update failed");
            ModelImportSettings check;
            Require(ReadModelImportSettings(metadata, check, &error) == ModelImportSettingsStatus::Success &&
                    check.meshOptions.ToFlags() == flags, "Import options changed");
        }
        loaded.materialRemaps = {{material, {}, "engine://builtin/material/default-shaded"}};
        Require(WriteModelImportSettings(metadata, loaded, &error), "Engine remap serialization failed");
        Require(ReadModelImportSettings(metadata, settings, &error) == ModelImportSettingsStatus::Success &&
                settings.materialRemaps[0].engineMaterial == "engine://builtin/material/default-shaded", "Engine remap changed");
        auto invalid = metadata;
        invalid.extensionRecords.push_back("MODEL_OBJECT\t42\t0\tmesh/duplicate");
        const auto previousId = settings.objects[0].localId;
        Require(ReadModelImportSettings(invalid, settings, &error) == ModelImportSettingsStatus::Invalid &&
                settings.objects[0].localId == previousId, "Duplicate ID accepted or failure changed output");
        invalid = metadata;
        invalid.extensionRecords.push_back("MODEL_OPTIONS\t8");
        Require(ReadModelImportSettings(invalid, settings, &error) == ModelImportSettingsStatus::Invalid, "Duplicate/unknown option flags accepted");
        invalid = {.id = "owner", .extensionRecords = {"MODEL_IMPORT\t3", "MODEL_OPTIONS\t7"}};
        const auto future = invalid.extensionRecords;
        Require(ReadModelImportSettings(invalid, settings, &error) == ModelImportSettingsStatus::UnsupportedVersion, "Future schema not rejected");
        Require(!WriteModelImportSettings(invalid, loaded, &error) && invalid.extensionRecords == future, "Future settings overwritten");
        {
            using namespace PlutoGE::assetimport;
            ModelImportSettings nodeSettings;
            Require(ResolveModelObjectId(nodeSettings, "mesh/main", 42) == 42, "Cannot reserve mesh identity");
            ImportedModelHierarchy hierarchy;
            hierarchy.nodes = {{"Arm", 1}, {"Root", -1}, {"Leg", 1}};
            std::vector<ModelNodeIdentity> identities;
            Require(ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error), "Initial node correspondence failed");
            const auto original = identities;
            Require(original.size() == 3 && original[0].localId && original[1].localId && original[2].localId &&
                original[0].localId != 42 && original[0].localId != original[2].localId, "Node IDs collide with native object IDs");
            hierarchy.nodes = {{"Root", -1}, {"Leg", 0}, {"Arm", 0}, {"New", 0}};
            hierarchy.nodes[2].localTransform[3].x = 25;
            Require(ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error) &&
                identities[0].localId == original[1].localId && identities[1].localId == original[2].localId &&
                identities[2].localId == original[0].localId, "Reordering, transforms or a new sibling reassigned node identities");
            hierarchy.nodes[2].name = "Renamed";
            Require(ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error) && identities[2].localId != original[0].localId &&
                std::any_of(nodeSettings.objects.begin(), nodeSettings.objects.end(), [&](const auto &entry)
                    { return entry.localId == original[0].localId && entry.retired; }), "Rename guessed correspondence or recycled an old ID");
            const auto renamedId = identities[2].localId;
            hierarchy.nodes[2].name = "Arm";
            Require(ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error) && identities[2].localId == original[0].localId &&
                std::any_of(nodeSettings.objects.begin(), nodeSettings.objects.end(), [&](const auto &entry)
                    { return entry.localId == renamedId && entry.retired; }), "Restored source key did not reactivate its tombstone");
            AssetMetadata nodeMetadata{.id="node-owner", .extensionRecords={"CUSTOM\tkeep"}};
            Require(WriteModelImportSettings(nodeMetadata, nodeSettings, &error), "Cannot persist node correspondence");
            ModelImportSettings roundTrip;
            Require(ReadModelImportSettings(nodeMetadata, roundTrip, &error) == ModelImportSettingsStatus::Success &&
                ReconcileModelNodeIdentities(hierarchy, roundTrip, identities, &error) && identities[2].localId == original[0].localId,
                "Node correspondence did not survive metadata round trip");
            hierarchy.nodes = {{"Root", -1}, {"Duplicate", 0}, {"Duplicate", 0}, {"Child", 1}, {"", 0}};
            Require(ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error) && identities[0].localId &&
                !identities[1].localId && identities[1].status == ModelNodeIdentityStatus::DuplicateSiblingName &&
                !identities[2].localId && !identities[3].localId && identities[3].status == ModelNodeIdentityStatus::UnresolvedAncestor &&
                !identities[4].localId && identities[4].status == ModelNodeIdentityStatus::Anonymous,
                "Ambiguous/anonymous source nodes received guessed identities");
            std::vector<std::string> sourceIds = {"root-id", "first-id", "second-id", "child-id", "anonymous-id"};
            Require(ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error, sourceIds), "Persistent producer IDs failed");
            const auto explicitIds = identities;
            hierarchy.nodes[1].name = "Different name";
            hierarchy.nodes[1].parentNodeIndex = -1;
            Require(ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error, sourceIds) &&
                identities[1].localId == explicitIds[1].localId, "Producer identity depended on node name or parent");
            sourceIds[2] = sourceIds[1];
            Require(ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error, sourceIds) &&
                !identities[1].localId && !identities[2].localId && identities[1].status == ModelNodeIdentityStatus::DuplicateSourceIdentifier,
                "Duplicate producer IDs silently fell back to structural guesses");
            const auto previousNodes = nodeSettings.objects.size();
            const auto previousOutput = identities[0].localId;
            hierarchy.nodes[0].parentNodeIndex = 2;
            Require(!ReconcileModelNodeIdentities(hierarchy, nodeSettings, identities, &error) &&
                nodeSettings.objects.size() == previousNodes && identities[0].localId == previousOutput,
                "Invalid node topology partially changed correspondence");
            ImportedModelHierarchy deep;
            deep.nodes.resize(2000);
            for (int index = 0; index < static_cast<int>(deep.nodes.size()); ++index)
            {
                deep.nodes[index].name = "Node";
                deep.nodes[index].parentNodeIndex = index + 1 < static_cast<int>(deep.nodes.size()) ? index + 1 : -1;
            }
            ModelImportSettings deepSettings;
            Require(ReconcileModelNodeIdentities(deep, deepSettings, identities, &error) && identities.front().localId &&
                identities.front().sourceKey.size() < 100, "Deep correspondence required recursion or unbounded path keys");
        }
        {
            using namespace PlutoGE::assetimport;
            ImportedModelHierarchy source;
            source.nodes = {{"Root", -1}, {"Arm", 0}, {"Hand", 1}, {"Other", 0}};
            ModelImportSettings originalSettings;
            std::vector<ModelNodeIdentity> originalNodes;
            Require(ReconcileModelNodeIdentities(source, originalSettings, originalNodes, &error), "Repair baseline failed");
            source.nodes[1].name = "RenamedArm";
            source.nodes[1].parentNodeIndex = 3;
            auto incomingSettings = originalSettings;
            std::vector<ModelNodeIdentity> incomingNodes;
            Require(ReconcileModelNodeIdentities(source, incomingSettings, incomingNodes, &error), "Repair incoming failed");
            const auto displacedId = incomingNodes[1].localId;
            ModelImportSettings repaired;
            std::vector<ModelNodeIdentity> repairedNodes;
            Require(PrepareModelNodeIdentityRepair(source, incomingSettings, 1, originalNodes[1].localId,
                repaired, repairedNodes, &error), "Reviewed rename/reparent repair failed");
            Require(repairedNodes[1].localId == originalNodes[1].localId &&
                repairedNodes[2].localId == originalNodes[2].localId && repairedNodes[3].localId == originalNodes[3].localId,
                "Repair did not restore parent and descendant correspondence");
            Require(std::any_of(repaired.objects.begin(), repaired.objects.end(), [&](const auto &object) {
                return object.localId == displacedId && object.retired && object.sourceKey.starts_with("node/tombstone/v1/");
            }), "Repair recycled the displaced identity");
            AssetMetadata repairedMetadata{.id="repair-owner", .extensionRecords={"CUSTOM\tkeep"}};
            Require(WriteModelImportSettings(repairedMetadata, repaired, &error), "Repair settings write failed");
            Require(std::find(repairedMetadata.extensionRecords.begin(), repairedMetadata.extensionRecords.end(),
                "MODEL_IMPORT\t2") != repairedMetadata.extensionRecords.end(), "Alias settings did not advance schema");
            ModelImportSettings restored;
            Require(ReadModelImportSettings(repairedMetadata, restored, &error) == ModelImportSettingsStatus::Success &&
                ReconcileModelNodeIdentities(source, restored, repairedNodes, &error) &&
                repairedNodes[1].localId == originalNodes[1].localId && repairedNodes[2].localId == originalNodes[2].localId,
                "Repair aliases did not survive metadata round trip");
            auto stableMetadata = repairedMetadata;
            Require(WriteModelImportSettings(stableMetadata, restored, &error) &&
                stableMetadata.extensionRecords == repairedMetadata.extensionRecords, "Repair serialization is unstable");
            auto badVersion = repairedMetadata;
            for (auto &line : badVersion.extensionRecords) if (line == "MODEL_IMPORT\t2") line = "MODEL_IMPORT\t1";
            Require(ReadModelImportSettings(badVersion, restored, &error) == ModelImportSettingsStatus::Invalid,
                "Legacy schema accepted node aliases");
            const auto stableCount = repaired.objects.size();
            const auto stableId = repairedNodes[1].localId;
            Require(!PrepareModelNodeIdentityRepair(source, incomingSettings, 1, originalNodes[0].localId,
                repaired, repairedNodes, &error) && repaired.objects.size() == stableCount && repairedNodes[1].localId == stableId,
                "Active target repair changed output");
            source.nodes.push_back({"Arm", 0});
            auto collisionSettings = repaired;
            Require(!ReconcileModelNodeIdentities(source, collisionSettings, repairedNodes, &error) &&
                collisionSettings.objects.size() == stableCount && repairedNodes[1].localId == stableId,
                "Two active nodes claimed one repaired identity or failure mutated outputs");
            source.nodes.pop_back();
            source.nodes.push_back({"Fresh", 0});
            Require(ReconcileModelNodeIdentities(source, restored, repairedNodes, &error) &&
                repairedNodes.back().localId != displacedId && repairedNodes.back().localId != originalNodes[1].localId,
                "Fresh node reused displaced identity");
            auto assertInvalid = [&](ModelImportSettings invalidSettings) {
                auto unchanged = repairedMetadata;
                Require(!WriteModelImportSettings(unchanged, invalidSettings, &error) &&
                    unchanged.extensionRecords == repairedMetadata.extensionRecords, "Invalid aliases changed metadata");
            };
            auto invalidSettings = repaired;
            invalidSettings.nodeAliases.push_back(invalidSettings.nodeAliases.front());
            assertInvalid(invalidSettings);
            invalidSettings = repaired;
            invalidSettings.nodeAliases[0].canonicalSourceKey = invalidSettings.nodeAliases[0].sourceKey;
            assertInvalid(invalidSettings);
            invalidSettings = repaired;
            invalidSettings.nodeAliases[0].canonicalSourceKey = "node/path/v1/" + std::string(64, 'a');
            assertInvalid(invalidSettings);
            invalidSettings = repaired;
            invalidSettings.nodeAliases[0].sourceKey = originalNodes[0].sourceKey;
            assertInvalid(invalidSettings);
            const auto originalRetired = invalidSettings.objects;
            Require(!ResolveModelObjectId(invalidSettings, originalNodes[0].sourceKey, 0, &error) &&
                std::equal(originalRetired.begin(), originalRetired.end(), invalidSettings.objects.begin(),
                    [](const auto &a, const auto &b) { return a.sourceKey == b.sourceKey && a.localId == b.localId && a.retired == b.retired; }),
                "Direct resolution accepted a colliding alias or changed identities on failure");
            invalidSettings = repaired;
            invalidSettings.nodeAliases[0].sourceKey = "node/path/v1/not-a-digest";
            assertInvalid(invalidSettings);
            invalidSettings = repaired;
            invalidSettings.nodeAliases.resize(4097, invalidSettings.nodeAliases.front());
            assertInvalid(invalidSettings);
            ImportedModelHierarchy anonymous;
            anonymous.nodes = {{"", -1}};
            Require(!PrepareModelNodeIdentityRepair(anonymous, incomingSettings, 0, originalNodes[1].localId,
                repaired, repairedNodes, &error), "Anonymous node repaired by array index");
        }
        std::cout << "Model import settings tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
