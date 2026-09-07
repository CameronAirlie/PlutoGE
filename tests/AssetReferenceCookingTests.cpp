#include "PlutoGE/assets/AssetDatabase.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }
    struct Scratch
    {
        const std::filesystem::path parent = std::filesystem::absolute(std::filesystem::temp_directory_path()).lexically_normal();
        const std::filesystem::path root = parent / ("PlutoGE-reference-cook-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root / "Assets"); }
        ~Scratch()
        {
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-reference-cook-"))
            {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        }
    };
    void Write(const std::filesystem::path &path, const std::string &value)
    {
        std::ofstream output(path, std::ios::binary);
        output << value;
        output.close();
        Require(static_cast<bool>(output), "Fixture write failed");
    }
}

int main()
{
    try
    {
        using namespace PlutoGE::assets;
        Scratch scratch;
        ProjectManifest manifest;
        manifest.startupScene = "project://Main.plutoscene";
        Project project(scratch.root / "Test.plutoproject", manifest);
        const auto assets = scratch.root / "Assets";
        Write(assets / "Main.plutoscene", "PROPERTY\tMaterial\t2\tproject://Stone.plutomaterial\t0\n");
        Write(assets / "Stone.plutomaterial", "AlbedoTexture=Stone texture.png\n");
        Write(assets / "Stone texture.png", "fixture");
        Write(assets / "Unused.png", "fixture");
        CookOptions options;
        options.includeUnreferencedAssets = false;
        std::string error;
        const auto cooked = scratch.root / "Cooked" / "Assets";
        auto success = CookProjectContent(project, cooked, options, &error);
        Require(success, "Cook failed: " + error);
        Require(std::filesystem::exists(cooked / "Stone texture.png"), "Transitive asset-relative material texture omitted");
        Require(!std::filesystem::exists(cooked / "Unused.png"), "Unreferenced asset was cooked");
        Write(assets / "Stone.plutomaterial", std::string(1024 * 1024 + 1, 'x') + "\n");
        Require(!CookProjectContent(project, cooked, options, &error), "Incomplete dependency scan should reject pruned cooking");
        Require(error.find("Stone.plutomaterial") != std::string::npos, "Scan error must identify its owner");
        options.includeUnreferencedAssets = true;
        success = CookProjectContent(project, scratch.root / "IncludeAll" / "Assets", options, &error);
        Require(success, "Include-all cooking should remain available: " + error);
        std::cout << "Asset reference cooking tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
