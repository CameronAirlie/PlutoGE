#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/ProjectValidation.h"
#include "PlutoGE/assets/SceneModelInstanceRecord.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::assets;
    void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        const std::filesystem::path parent = std::filesystem::absolute(std::filesystem::temp_directory_path()).lexically_normal();
        const std::filesystem::path root = parent / ("PlutoGE-validation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root); }
        ~Scratch()
        {
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-validation-"))
            { std::error_code ec; std::filesystem::remove_all(root, ec); }
        }
    };
    void Write(const std::filesystem::path &path, const std::string &text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary); file << text; file.close();
        Require(static_cast<bool>(file), "Fixture write failed");
    }
    bool Has(const ProjectValidationResult &result, const std::string &code, std::uint32_t entity = 0)
    {
        return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [&](const auto &d) { return d.code == code && (!entity || d.entity == entity); });
    }
    const std::string entity = "ENTITY\t1\t0\t1\tPlayer\t0,0,0\t0,0,0\t1,1,1\n";
    const std::string camera = "COMPONENT\t1\tCameraComponent\t1\nEND_COMPONENT\n";
    void ManagedValidation(const std::filesystem::path &root)
    {
        ProjectValidationInput input;
        input.assetRoot = root; input.projectRoot = root; input.assetPipelineVersion = 5;
        input.scriptClasses = std::set<std::string>{"Game.Player"};
        input.startupScene = "project://Main.plutoscene";
        Write(root / "Paint.plutomaterial", "Color=1,1,1,1\n");
        const std::string component = "COMPONENT\t1\tScriptComponent\t1\n"
            "PROPERTY\tSource\t2\tGame.Player\t0\n"
            "PROPERTY\tPaint\t2\tproject://Paint.plutomaterial\t0\n"
            "PROPERTY\tText\t2\tproject://Missing.plutomaterial\t0\n"
            "PROPERTY\t__Pluto.AssetFields.Version\t2\t1\t0\n"
            "PROPERTY\t__Pluto.AssetField.Paint\t2\tMaterial\t0\nEND_COMPONENT\n";
        const auto good = "SCENE\t3\n" + entity + camera + component;
        Write(root / "Main.plutoscene", good);
        Require(ValidateProject(input).diagnostics.empty(), "Typed disk fields misclassified an ordinary string");
        input.currentScene = good;
        input.currentSceneOwner = "project://Main.plutoscene";
        Require(ValidateProject(input).diagnostics.empty(), "Typed current scene rejected without reflection metadata");
        auto wrong = good;
        wrong.replace(wrong.find("project://Paint.plutomaterial"), std::string("project://Paint.plutomaterial").size(), "project://Paint.plutoprefab");
        input.currentScene = wrong;
        Require(Has(ValidateProject(input), "asset.type"), "Wrong managed asset kind accepted");
        auto logical = good;
        logical.replace(logical.find("project://Paint.plutomaterial"), std::string("project://Paint.plutomaterial").size(), "asset://paint#1099511627776");
        input.currentScene = logical;
        auto catalog = std::make_shared<AssetCatalog>();
        Require(catalog->Replace({{.identity={"paint",1099511627776ull}, .type=ProjectAssetType::Material,
            .location="project://Paint.plutomaterial"}}), "Managed validation catalog invalid");
        input.assetCatalog = catalog;
        Require(ValidateProject(input).diagnostics.empty(), "Managed 64-bit logical identity rejected");
        Require(catalog->Replace({{.identity={"paint",1099511627776ull}, .type=ProjectAssetType::Prefab,
            .location="project://Paint.plutomaterial"}}), "Wrong-kind catalog invalid");
        Require(Has(ValidateProject(input), "asset.type"), "Logical catalog type mismatch accepted");
        Require(catalog->Replace({}), "Cannot clear managed validation catalog");
        Require(Has(ValidateProject(input), "asset.missing"), "Missing typed logical asset accepted");
        input.currentScene = good;
        input.currentScene->replace(0, std::string("SCENE\t3").size(), "SCENE\t1");
        Require(Has(ValidateProject(input), "script.asset_schema"), "Legacy scene accepted field role schema");
        input.currentScene = good;
        const auto schema = input.currentScene->find("__Pluto.AssetFields.Version\t2\t1");
        input.currentScene->replace(schema, std::string("__Pluto.AssetFields.Version\t2\t1").size(), "__Pluto.AssetFields.Version\t2\t2");
        Require(Has(ValidateProject(input), "script.asset_schema"), "Future field schema accepted");
    }
    void LinkedValidation(const std::filesystem::path &root)
    {
        StaticModelInstanceState state;
        state.rootEntityId = 1; state.artifactGenerationKey[0] = 1;
        state.packageArtifact = {"project://Accepted.plutomodel", {}}; state.packageArtifact.digest[0] = 2;
        state.accepted.layout.sourceAssetId = "source"; state.accepted.layout.meshReference = "asset://source#2";
        state.accepted.layout.hierarchyDigest[0] = 3; state.accepted.meshDigest[0] = 4;
        state.accepted.submeshCount = 1; state.accepted.materialSlotCount = 1;
        state.accepted.layout.nodes = {{1ull << 45, "Source", -1, glm::mat4(1)}};
        state.accepted.layout.bindings = {{0, 0, glm::mat4(1)}};
        state.defaultMaterials = {""};
        state.overrides.hierarchyDigest = state.accepted.layout.hierarchyDigest; state.overrides.meshDigest = state.accepted.meshDigest;
        state.nodeEntities = {{1ull << 45, 2}}; state.bindingEntities = {3};
        std::string record;
        Require(SerializeSceneModelInstanceRecord(state, record), "Linked validation fixture invalid");
        ProjectValidationInput input;
        input.assetRoot = root; input.projectRoot = root; input.assetPipelineVersion = 5;
        input.currentScene = "SCENE\t3\n" + record + "\n" + entity + camera +
            "ENTITY\t2\t1\t1\tSource\t0,0,0\t0,0,0\t1,1,1\n"
            "ENTITY\t3\t2\t1\tGeometry\t0,0,0\t0,0,0\t1,1,1\n"
            "COMPONENT\t3\tMeshComponent\t1\nPROPERTY\tMesh\t2\tasset://source#2\t0\nEND_COMPONENT\n";
        Require(Has(ValidateProject(input), "model.generation"), "Missing generation verifier accepted linked scene");
        auto catalog = std::make_shared<AssetCatalog>();
        Require(catalog->Replace({{{"source", 2}, ProjectAssetType::Mesh, AssetOwnership::Imported, "Accepted", "project://Private.plutomesh"}}), "Private catalog invalid");
        input.prepareModelInstance = [catalog](const auto &, std::string *) { return catalog; };
        Require(!Has(ValidateProject(input), "model.generation") && !Has(ValidateProject(input), "model.reference") &&
            !Has(ValidateProject(input), "asset.missing"), "Private accepted reference used current catalog");
        input.currentScene->replace(input.currentScene->find("PROPERTY\tMesh\t2\tasset://source#2"), std::string("PROPERTY\tMesh\t2\tasset://source#2").size(),
            "PROPERTY\tMesh\t2\tasset://source#9");
        Require(Has(ValidateProject(input), "model.reference"), "Missing private dependency accepted");
        input.assetPipelineVersion = 4;
        Require(Has(ValidateProject(input), "scene.header"), "Legacy project accepted linked format");
    }
}
int main()
{
    try
    {
        Scratch scratch;
        { Scratch linked; LinkedValidation(linked.root); }
        { Scratch managed; ManagedValidation(managed.root); }
        ProjectValidationInput input;
        input.assetRoot = scratch.root; input.projectRoot = scratch.root;
        input.startupScene = "project://Main.plutoscene";
        input.scriptClasses = std::set<std::string>{"Game.Player"};
        input.builtinReferences = {"engine://builtin/mesh/cube"};
        const auto main = scratch.root / "Main.plutoscene";
        const std::string good = "SCENE\t1\n" + entity + camera +
            "COMPONENT\t1\tScriptComponent\t1\nPROPERTY\tSource\t2\tGame.Player\t0\nEND_COMPONENT\n";
        Write(main, good);
        Write(scratch.root / "ModelSnapshots/Retained.plutoscene", "invalid retained infrastructure");
        Write(scratch.root / "Library/Imported.plutoscene", "invalid cache infrastructure");
        Write(scratch.root / ".pluto-generations/private.plutoscene", "invalid packed infrastructure");
        auto result = ValidateProject(input);
        Require(result.diagnostics.empty() && result.checkedFiles == 1, "Valid scene or private infrastructure filtering produced diagnostics");
        {
            Scratch nested;
            Write(nested.root / "Assets/Library/Main.plutoscene", good);
            auto nestedInput = input;
            nestedInput.assetRoot = nested.root / "Assets"; nestedInput.projectRoot = nested.root;
            nestedInput.startupScene = "project://Library/Main.plutoscene";
            const auto nestedResult = ValidateProject(nestedInput);
            Require(nestedResult.diagnostics.empty() && nestedResult.checkedFiles == 1,
                "Authored nested Library directory was mistaken for project infrastructure");
        }
        input.currentScene = good + "COMPONENT\t1\tMeshComponent\t1\nPROPERTY\tMesh\t2\tasset://model-owner#42\t0\nEND_COMPONENT\n";
        input.currentSceneOwner = "project://Main.plutoscene";
        Require(Has(ValidateProject(input), "asset.unverified"), "Logical reference without catalog silently ignored");
        auto logicalCatalog = std::make_shared<AssetCatalog>();
        Require(logicalCatalog->Replace({{.identity={"model-owner",42}, .type=ProjectAssetType::Mesh,
                    .location="engine://builtin/mesh/cube"}}), "Cannot create validation catalog");
        input.assetCatalog = logicalCatalog;
        Require(ValidateProject(input).diagnostics.empty(), "Catalog-backed logical reference rejected");
        Require(logicalCatalog->Replace({}), "Cannot clear validation catalog");
        Require(Has(ValidateProject(input), "asset.missing"), "Missing logical object not reported");
        input.currentScene.reset();
        input.assetCatalog.reset();
        const std::string terrainScene = good + "COMPONENT\t1\tTerrainComponent\t1\nPROPERTY\tHeightSamples\t2\t" +
            std::string(3 * 1024 * 1024, '0') + "\t0\nEND_COMPONENT\n";
        Write(main, terrainScene);
        Require(ValidateProject(input).diagnostics.empty(), "Large inline terrain record rejected on disk");
        input.currentScene = terrainScene;
        input.currentSceneOwner = "project://Main.plutoscene";
        Require(ValidateProject(input).diagnostics.empty(), "Large inline terrain record rejected in current scene");
        input.currentScene.reset();
        Write(main, "SCENE\t1\n" + entity + "COMPONENT\t1\tMeshComponent\t1\nPROPERTY\tMesh\t2\tproject://Missing mesh.plutomesh\t0\nEND_COMPONENT\n");
        result = ValidateProject(input);
        Require(result.HasErrors() && Has(result, "asset.missing", 1) && Has(result, "camera.missing"), "Missing assets/cameras not reported");
        Require(std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [](const auto &d) { return d.code == "asset.missing" && d.owner == "project://Main.plutoscene" && d.line == 4; }), "Reference owner/line incorrect");
        input.currentScene = good;
        input.currentSceneOwner = "project://Main.plutoscene";
        Require(ValidateProject(input).diagnostics.empty(), "Current scene did not replace stale disk scene");
        input.currentScene = "SCENE\t1\n" + entity + camera + "COMPONENT\t1\tScriptComponent\t1\nPROPERTY\tSource\t2\tMissing.Class\t0\nEND_COMPONENT\n";
        Require(Has(ValidateProject(input), "script.missing", 1), "Missing script class not reported");
        input.scriptClasses.reset();
        result = ValidateProject(input);
        Require(!result.HasErrors() && Has(result, "script.unverified"), "Unavailable catalogue falsely reports missing class");
        input.currentScene = "SCENE\t1\n" + entity + camera + "COMPONENT\t1\tColliderComponent\t1\nPROPERTY\tShape\t3\t2\t0\nPROPERTY\tRadius\t0\t2\t0\nPROPERTY\tHeight\t0\t1\t0\nEND_COMPONENT\n";
        Require(Has(ValidateProject(input), "collider.invalid", 1), "Bad capsule was not detected before clamping");
        for (const auto &size : {"0,1,1", "-1,1,1", "nan,1,1", "1e100,1,1", "1e-60,1,1", "1,2", "1,2,3junk"})
        {
            input.currentScene = "SCENE\t1\n" + entity + camera + "COMPONENT\t1\tColliderComponent\t1\nPROPERTY\tSize\t0\t" + size + "\t0\nEND_COMPONENT\n";
            Require(Has(ValidateProject(input), "collider.invalid"), "Invalid box size accepted");
        }
        input.currentScene = "SCENE\t1\n" + entity + camera + "COMPONENT\t1\tColliderComponent\t1\nPROPERTY\tShape\t3\t4\t0\nEND_COMPONENT\n";
        Require(Has(ValidateProject(input), "collider.invalid"), "Missing mesh source accepted");
        input.currentScene = "SCENE\t1\nENTITY\t2\t0\t0\tParent\t0,0,0\t0,0,0\t1,1,1\nENTITY\t1\t2\t1\tChild\t0,0,0\t0,0,0\t1,1,1\n" + camera;
        result = ValidateProject(input);
        Require(Has(result, "camera.missing") && !result.HasErrors(), "Inactive camera hierarchy should warn");
        input.currentScene = "SCENE\t1\nENTITY\t1\t1\t1\tCycle\t0,0,0\t0,0,0\t1,1,1\n" + camera;
        Require(Has(ValidateProject(input), "scene.hierarchy"), "Hierarchy cycle not detected");
        input.currentScene = "broken";
        Require(Has(ValidateProject(input), "scene.header"), "Invalid scene header accepted");
        input.currentScene = "SCENE\t2\n" + entity + camera + "LINEAR_TRANSFORM\t1\t1,0,0,0.5,1,0,0,0,1\n";
        Require(Has(ValidateProject(input), "scene.header"), "Legacy project accepted affine format");
        input.assetPipelineVersion = 4;
        Require(!ValidateProject(input).HasErrors(), "Affine scene validation failed");
        input.currentScene = *input.currentScene + "LINEAR_TRANSFORM\t1\t1,0,0,0,1,0,0,0,1\n";
        Require(ValidateProject(input).HasErrors(), "Duplicate linear transform passed validation");
        input.currentScene = "SCENE\t2\n" + entity + camera + "LINEAR_TRANSFORM\t1\tnan,0,0,0,1,0,0,0,1\n";
        Require(ValidateProject(input).HasErrors(), "Non-finite linear transform passed validation");
        input.assetPipelineVersion = 1;
        input.currentScene.reset();
        Write(main, good);
        Write(scratch.root / "Prefab.plutoprefab", "SCENE\t1\n" + entity);
        Write(scratch.root / "Stone.plutomaterial", "AlbedoTexture=Stone texture.png\nShaderGraph=engine://builtin/mesh/cube\n");
        Write(scratch.root / "Stone texture.png", "fixture");
        result = ValidateProject(input);
        Require(!result.HasErrors() && !Has(result, "camera.missing"), "Prefab camera or valid material incorrectly reported");
        std::filesystem::remove(scratch.root / "Stone texture.png");
        Require(Has(ValidateProject(input), "asset.missing"), "Material relative texture not checked");
        input.scriptAssembly = scratch.root / "Missing.dll";
        Require(Has(ValidateProject(input), "script.assembly"), "Missing assembly not reported");
        input.startupScene = "invalid";
        Require(Has(ValidateProject(input), "project.startup"), "Invalid startup reference accepted");
        input.assetRoot = scratch.root / "missing";
        Require(Has(ValidateProject(input), "scan.incomplete"), "Enumeration failure not reported");
        for (const auto &file : std::filesystem::recursive_directory_iterator(scratch.root))
            Require(file.path().extension() != ".plutometa", "Validation wrote metadata");
        std::cout << "PASS: project validation references, owners, scripts, cameras, colliders, hierarchy, current scene and errors\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
