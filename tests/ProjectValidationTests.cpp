#include "PlutoGE/assets/ProjectValidation.h"
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
        std::ofstream file(path, std::ios::binary); file << text; file.close();
        Require(static_cast<bool>(file), "Fixture write failed");
    }
    bool Has(const ProjectValidationResult &result, const std::string &code, std::uint32_t entity = 0)
    {
        return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [&](const auto &d) { return d.code == code && (!entity || d.entity == entity); });
    }
    const std::string entity = "ENTITY\t1\t0\t1\tPlayer\t0,0,0\t0,0,0\t1,1,1\n";
    const std::string camera = "COMPONENT\t1\tCameraComponent\t1\nEND_COMPONENT\n";
}
int main()
{
    try
    {
        Scratch scratch;
        ProjectValidationInput input;
        input.assetRoot = scratch.root;
        input.startupScene = "project://Main.plutoscene";
        input.scriptClasses = std::set<std::string>{"Game.Player"};
        input.builtinReferences = {"engine://builtin/mesh/cube"};
        const auto main = scratch.root / "Main.plutoscene";
        const std::string good = "SCENE\t1\n" + entity + camera +
            "COMPONENT\t1\tScriptComponent\t1\nPROPERTY\tSource\t2\tGame.Player\t0\nEND_COMPONENT\n";
        Write(main, good);
        auto result = ValidateProject(input);
        Require(result.diagnostics.empty(), "Valid scene produced diagnostics");
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
