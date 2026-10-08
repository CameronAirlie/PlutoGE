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
        invalid = {.id = "owner", .extensionRecords = {"MODEL_IMPORT\t2", "MODEL_OPTIONS\t7"}};
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
        std::cout << "Model import settings tests passed\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
