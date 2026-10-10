#include <PlutoGE/assets/ModelInstanceState.h>
#include <PlutoGE/assets/SceneModelInstanceRecord.h>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::assets;
    using namespace std::string_literals;
    constexpr std::uint64_t SourceNode = (1ull << 45) + 3;
    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
    StaticModelInstanceState State()
    {
        StaticModelInstanceState state;
        state.packageArtifact.reference = "project://Models/Source.plutomodel"; state.packageArtifact.digest[0] = 8;
        state.rootEntityId = 12; state.artifactGenerationKey[0] = 3;
        state.accepted.layout.sourceAssetId = "source"; state.accepted.layout.meshReference = "asset://source#2";
        state.accepted.layout.hierarchyDigest[0] = 4; state.accepted.meshDigest[0] = 5;
        state.accepted.submeshCount = 1; state.accepted.materialSlotCount = 2;
        auto shear = glm::mat4(1); shear[1][0] = 0.375f; shear[0][0] = -2;
        state.accepted.layout.nodes = {{SourceNode, "Part\t\n\0"s, -1, shear}};
        state.accepted.layout.bindings = {{0, 0, glm::mat4(1)}};
        state.defaultMaterials = {"asset://material#0", "engine://builtin/material/default"};
        state.overrides.hierarchyDigest = state.accepted.layout.hierarchyDigest;
        state.overrides.meshDigest = state.accepted.meshDigest;
        state.overrides.nodes = {{SourceNode, shear, false, true, true, true}};
        state.overrides.materials = {{0, 1, ""}};
        state.nodeEntities = {{SourceNode, 13}}; state.bindingEntities = {14};
        return state;
    }
}
int main() try
{
    using namespace PlutoGE::assets;
    auto state = State(); std::string bytes, error;
    Require(SerializeStaticModelInstanceState(state, bytes, &error), error);
    StaticModelInstanceState reopened;
    Require(ParseStaticModelInstanceState(bytes, reopened, &error), error);
    Require(reopened.packageArtifact.reference == state.packageArtifact.reference && reopened.packageArtifact.digest == state.packageArtifact.digest &&
        reopened.rootEntityId == 12 && reopened.nodeEntities[0].sourceNodeId == SourceNode && reopened.nodeEntities[0].sceneEntityId == 13 &&
        reopened.bindingEntities[0] == 14 && reopened.accepted.layout.nodes[0].localTransform == state.accepted.layout.nodes[0].localTransform &&
        reopened.accepted.layout.nodes[0].name == state.accepted.layout.nodes[0].name && reopened.overrides.nodes[0].enabled == false &&
        reopened.overrides.nodes[0].hasStructuralEdits && reopened.overrides.nodes[0].hasGeometryEdits && reopened.overrides.materials[0].reference.empty(),
        "State round trip lost source IDs, exact affine state, names, scene mapping or overrides");
    std::string again;
    Require(SerializeStaticModelInstanceState(reopened, again, &error) && again == bytes, "Codec round trip was not byte exact");
    auto granular = state;
    granular.overrides.nodes[0].localTransform.reset();
    granular.overrides.nodes[0].localPosition = glm::vec3(0);
    granular.overrides.nodes[0].localRotation = glm::vec3(-25,180,720);
    granular.overrides.nodes[0].localScale = glm::vec3(0,-2,3);
    std::string granularBytes;
    Require(SerializeStaticModelInstanceState(granular, granularBytes, &error) &&
        static_cast<unsigned char>(granularBytes[std::string_view("PLUTOMODELINSTANCE").size()]) == 4 &&
        ParseStaticModelInstanceState(granularBytes, reopened, &error), error);
    Require(reopened.overrides.nodes[0].localPosition == granular.overrides.nodes[0].localPosition &&
        reopened.overrides.nodes[0].localRotation == granular.overrides.nodes[0].localRotation &&
        reopened.overrides.nodes[0].localScale == granular.overrides.nodes[0].localScale &&
        SerializeStaticModelInstanceState(reopened, again, &error) && again == granularBytes,
        "Granular transform intent was not byte-exact");
    auto unsupportedGranular = granularBytes;
    unsupportedGranular[std::string_view("PLUTOMODELINSTANCE").size()] = 3;
    reopened.rootEntityId = 999;
    Require(!ParseStaticModelInstanceState(unsupportedGranular, reopened, &error) && reopened.rootEntityId == 999,
        "Older payload version silently accepted granular controls");
    for (std::size_t size = 0; size < granularBytes.size(); ++size)
        Require(!ParseStaticModelInstanceState(granularBytes.substr(0,size), reopened, &error),
            "Truncated granular transform payload was accepted");
    std::string record;
    Require(SerializeSceneModelInstanceRecord(state, record, &error), error);
    Require(record.starts_with("MODEL_INSTANCE\t1\t") && ParseSceneModelInstanceRecord(record, reopened, &error), error);
    Require(SerializeStaticModelInstanceState(reopened, again, &error) && again == bytes, "Scene record changed binary evidence");
    for (const auto &invalidRecord : {std::string("MODEL_INSTANCE\t2\t") + record.substr(17), record + "0", record + "gg", std::string("MODEL_INSTANCE\t1\tAA")})
    {
        reopened.rootEntityId = 999;
        Require(!ParseSceneModelInstanceRecord(invalidRecord, reopened, &error) && reopened.rootEntityId == 999,
            "Invalid scene record replaced accepted state");
    }
    const auto reject = [&](const std::string &invalid)
    {
        reopened.rootEntityId = 999;
        Require(!ParseStaticModelInstanceState(invalid, reopened, &error) && reopened.rootEntityId == 999 && !error.empty(),
            "Invalid payload replaced previous state");
    };
    for (std::size_t size = 0; size < bytes.size(); ++size) reject(bytes.substr(0, size));
    reject(bytes + "trailing");
    auto future = bytes; future[std::string_view("PLUTOMODELINSTANCE").size()] = 5; reject(future);
    auto legacy = state; legacy.overrides.nodes[0].hasGeometryEdits = false;
    std::string legacyBytes;
    Require(SerializeStaticModelInstanceState(legacy, legacyBytes, &error), error);
    legacyBytes[std::string_view("PLUTOMODELINSTANCE").size()] = 2;
    Require(ParseStaticModelInstanceState(legacyBytes, reopened, &error) && reopened.overrides.nodes[0].hasGeometryEdits,
        "Legacy additional edits did not conservatively protect accepted geometry");
    auto invalidLegacy = bytes; invalidLegacy[std::string_view("PLUTOMODELINSTANCE").size()] = 2; reject(invalidLegacy);
    auto invalidMagic = bytes; invalidMagic[0] = 'X'; reject(invalidMagic);
    const auto rejectState = [&](const StaticModelInstanceState &invalid)
    {
        again = "prior";
        Require(!SerializeStaticModelInstanceState(invalid, again, &error) && again == "prior" && !error.empty(),
            "Invalid state replaced previous serialized bytes");
    };
    auto invalid = state; invalid.nodeEntities[0].sceneEntityId = state.rootEntityId; rejectState(invalid);
    invalid = state; invalid.bindingEntities[0] = state.nodeEntities[0].sceneEntityId; rejectState(invalid);
    invalid = state; invalid.nodeEntities[0].sourceNodeId = 7; rejectState(invalid);
    invalid = state; invalid.bindingEntities.clear(); rejectState(invalid);
    invalid = state; invalid.artifactGenerationKey = {}; rejectState(invalid);
    invalid = state; invalid.packageArtifact.digest = {}; rejectState(invalid);
    invalid = state; invalid.packageArtifact.reference = "project://../Source.plutomodel"; rejectState(invalid);
    invalid = state; invalid.packageArtifact.reference = "asset://source#1"; rejectState(invalid);
    invalid = state; invalid.overrides.meshDigest = {}; rejectState(invalid);
    invalid = state; invalid.defaultMaterials.pop_back(); rejectState(invalid);
    invalid = state; invalid.defaultMaterials[0] = "asset://broken#invalid"; rejectState(invalid);
    StaticModelInstanceReconciliation result;
    auto incoming = state.accepted; incoming.layout.hierarchyDigest[0] = 6;
    incoming.layout.nodes.clear(); incoming.layout.bindings.clear();
    Require(PrepareStaticModelInstanceReconciliation(reopened.accepted, reopened.overrides, incoming, result, &error) &&
        !result.CanPublish() && result.generation.meshDigest == state.accepted.meshDigest,
        "Reopened accepted baseline did not retain conflicted edits");
    std::cout << "Model instance state tests passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
