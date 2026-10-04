#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
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
            PlutoGE::content::UnmountAll();
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

int main(int argc, char **argv)
{
    try
    {
        using namespace PlutoGE::assets;
        Scratch scratch;
        ProjectManifest manifest;
        manifest.startupScene = "project://Main.plutoscene";
        Project project(scratch.root / "Test.plutoproject", manifest);
        const auto assets = scratch.root / "Assets";
        const auto staleRuntime = scratch.root / "stale-runtime.exe";
        Write(staleRuntime, "Old runtime fixture");
        Require(!IsRuntimeContentPackCompatible(staleRuntime), "Stale runtime accepted");
        std::string compatibilityError;
        const auto rejectedOutput = scratch.root / "Rejected/Game.exe";
        Require(!ExportStandaloneProject(project, rejectedOutput, staleRuntime, &compatibilityError), "Export accepted a stale runtime");
        Require(!std::filesystem::exists(rejectedOutput.parent_path()), "Rejected export modified its destination");
        Write(staleRuntime, std::string(65530, 'x') + std::string(kRuntimeContentPackMarker));
        Require(IsRuntimeContentPackCompatible(staleRuntime), "Runtime marker crossing a read boundary was missed");
#ifdef _WIN32
        const auto runtimeName = "PlutoGERuntime.exe";
#else
        const auto runtimeName = "PlutoGERuntime";
#endif
        const auto candidates = scratch.root / "RuntimeCandidates";
        std::filesystem::create_directories(candidates / "old");
        std::filesystem::create_directories(candidates / "compatible");
        Write(candidates / "compatible" / runtimeName, std::string(kRuntimeContentPackMarker));
        Write(candidates / "old" / runtimeName, "stale runtime");
        Require(FindRuntimeExecutable(candidates) == candidates / "compatible" / runtimeName, "Discovery selected an incompatible runtime");
        Write(assets / "Main.plutoscene", "PROPERTY\tMaterial\t2\tproject://Stone.plutomaterial\t0\n");
        Write(assets / "Stone.plutomaterial", "AlbedoTexture=Stone texture.png\n");
        Write(assets / "Stone texture.png", "fixture");
        Write(assets / "Flash.plutoparticles", "ParticleSystemVersion=2\r\nMaterialAsset=project://Flash.plutomaterial\r\n");
        Write(assets / "Flash.plutomaterial", "AlphaMode=Mask\r\nAlbedoTexture=project://Flash.png\r\nShaderGraph=engine://builtin/shadergraph/default-unlit\r\n");
        const unsigned char flashPng[] = {0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0xf4,0x22,0x7f,0x8a,0x00,0x00,0x00,0x11,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xfc,0xcf,0xc0,0xc0,0xc0,0xf0,0x9f,0xe1,0x3f,0x00,0x0c,0x06,0x02,0xff,0x2c,0xa5,0x15,0xdd,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        Write(assets / "Flash.png", {reinterpret_cast<const char *>(flashPng), sizeof(flashPng)});
        Write(assets / "Section.plutoscene", "SCENE\t1\nPROPERTY\tMaterial\t2\tproject://Section.plutomaterial\t0\n");
        Write(assets / "Section.plutomaterial", "AlbedoTexture=Section.png\n");
        Write(assets / "Section.png", "fixture");
        { std::ofstream scene(assets / "Main.plutoscene", std::ios::app);
          scene << "PROPERTY\tParticleSystemAsset\t2\tproject://Flash.plutoparticles\t0\n";
          scene << "PROPERTY\tField.sceneAsset\t2\tproject://Section.plutoscene\t0\n"; }
        Write(assets / "Unused.png", "fixture");
        Write(assets / "Paladin.glb", "direct model fixture");
        { std::ofstream scene(assets / "Main.plutoscene", std::ios::app);
          scene << "PROPERTY\tMeshAssetReference\t2\tproject://Paladin.glb\t0\n"; }
        Write(assets / "Robot.fbx", "source model fixture");
        Write(assets / "Robot.plutomodel", "PLUTOMODEL\t1\nSOURCE\tproject://Robot.fbx\nOBJECT\t42\tMesh\tRobot\tproject://Robot.plutomesh\n");
        Write(assets / "Robot.plutomesh", "fixture");
        CookOptions options;
        options.includeUnreferencedAssets = false;
        options.alwaysInclude = {"project://Robot.fbx"};
        std::string error;
        const auto cooked = scratch.root / "Cooked" / "Assets";
        auto success = CookProjectContent(project, cooked, options, &error);
        Require(success, "Cook failed: " + error);
        Require(std::filesystem::exists(cooked / "Stone texture.png"), "Transitive asset-relative material texture omitted");
        Require(std::filesystem::exists(cooked / "Flash.png") && std::filesystem::exists(cooked / "Flash.plutomaterial"), "Particle material/texture omitted from pruned cook");
        Require(std::filesystem::exists(cooked / "Section.plutoscene") && std::filesystem::exists(cooked / "Section.png"), "Additive scene or transitive section assets omitted from pruned cook");
        Require(std::filesystem::exists(cooked / "Paladin.glb"), "Direct runtime model was omitted");
        Require(!std::filesystem::exists(cooked / "Unused.png"), "Unreferenced asset was cooked");
        Require(!std::filesystem::exists(cooked / "Robot.fbx"), "Source model shipped");
        Require(std::filesystem::exists(cooked / "Robot.plutomodel"), "Runtime model manifest omitted");
        Require(std::filesystem::exists(cooked / "Robot.plutomesh"), "Model object dependency omitted");
        AssetDatabase identities;
        Require(identities.Scan(project, &error), error);
        const auto stoneId = identities.FindByReference("project://Stone texture.png")->id;
        const auto modelId = identities.FindByReference("project://Robot.fbx")->id;
        Project exported(scratch.root / "Cooked/Game.plutoproject", project.GetManifest());
        exported.RefreshAssetRegistry();
        Require(exported.Save(&error), error);
        const auto packPath = scratch.root / "Game.plutopack";
        Require(PlutoGE::content::WritePack(scratch.root / "Cooked", packPath, {}, &error), error);
        const auto virtualRoot = scratch.root / "Mounted";
        Require(PlutoGE::content::Mount(packPath, virtualRoot, &error), error);
        auto mountedProject = Project::Load(virtualRoot / "Game.plutoproject", &error);
        Require(bool(mountedProject), "Mounted project load: " + error);
        Require(mountedProject->FindSceneAssetReference("project://Main.plutoscene") == "project://Main.plutoscene", "Mounted scene lookup");
        AssetManager manager;
        manager.SetProjectContext(virtualRoot.string());
        const auto flash = manager.LoadParticleSystemAsset("project://Flash.plutoparticles");
        Require(flash.materialAssetReference == "project://Flash.plutomaterial", "Packed particle material path contains CRLF");
        const auto *flashMaterial = manager.LoadMaterialAsset(flash.materialAssetReference);
        Require(flashMaterial && flashMaterial->ReadConfig().alphaMode == PlutoGE::render::AlphaMode::Mask, "Packed material alpha mode lost");
        const auto *flashTexture = flashMaterial->ReadConfig().albedoTexture;
        Require(flashTexture && flashTexture->GetRgba8Pixels().size() == 8, "Packed particle texture failed to decode");
        Require(flashTexture->GetRgba8Pixels()[3] == 0 && flashTexture->GetRgba8Pixels()[7] == 255, "Packed particle texture lost transparency");
        Require(manager.GetStableAssetId("project://Stone texture.png") == stoneId, "Packed stable asset identity");
        Require(manager.ResolveStableAssetId(stoneId) == "project://Stone texture.png", "Packed stable asset resolution");
        Require(manager.ResolveModelObject(modelId, 42) == "project://Robot.plutomesh", "Packed model object resolution without source bytes");
        Require(!std::filesystem::exists(virtualRoot), "Project loading extracted content");
        PlutoGE::content::UnmountAll();
        options.alwaysInclude = {"project://Unused.png"};
        Require(CookProjectContent(project, scratch.root / "Explicit/Assets", options, &error), error);
        Require(std::filesystem::exists(scratch.root / "Explicit/Assets/Unused.png"), "Explicit inclusion omitted");
        options.alwaysInclude = {"project://Missing.png"};
        Require(!CookProjectContent(project, scratch.root / "Missing/Assets", options, &error), "Missing explicit root accepted");
        options.alwaysInclude.clear();
        Write(assets / "Stone.plutomaterial", std::string(1024 * 1024 + 1, 'x') + "\n");
        Require(!CookProjectContent(project, cooked, options, &error), "Incomplete dependency scan should reject pruned cooking");
        Require(error.find("Stone.plutomaterial") != std::string::npos, "Scan error must identify its owner");
        options.includeUnreferencedAssets = true;
        success = CookProjectContent(project, scratch.root / "IncludeAll" / "Assets", options, &error);
        Require(success, "Include-all cooking should remain available: " + error);
        if (argc > 1)
        {
            const auto gameRoot = scratch.root / "GameMounted";
            Require(PlutoGE::content::Mount(argv[1], gameRoot, &error), error);
            manager.SetProjectContext(gameRoot.string());
            const auto muzzle = manager.LoadParticleSystemAsset("project://Particles/MuzzleFlash.plutoparticles");
            Require(muzzle.materialAssetReference == "project://Materials/MuzzleFlash.plutomaterial", "Game muzzle material path failed");
            for (const auto &reference : {muzzle.materialAssetReference, std::string("project://Materials/BulletHole.plutomaterial")})
            {
                const auto *material = manager.LoadMaterialAsset(reference);
                Require(material && material->ReadConfig().albedoTexture, "Game effect texture missing: " + reference);
                const auto pixels = material->ReadConfig().albedoTexture->GetRgba8Pixels();
                bool transparent = false, opaque = false;
                for (std::size_t i = 3; i < pixels.size(); i += 4) { transparent |= pixels[i] == 0; opaque |= pixels[i] > 128; }
                Require(transparent && opaque, "Game effect alpha mask lost: " + reference);
                std::cout << "Packed effect verified: " << reference << '\n';
            }
        }
        std::cout << "Asset reference cooking tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
