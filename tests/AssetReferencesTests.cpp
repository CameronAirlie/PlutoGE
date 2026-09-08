#include "PlutoGE/assets/AssetReferences.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::assets;

    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Scratch
    {
        const std::filesystem::path parent = std::filesystem::absolute(std::filesystem::temp_directory_path()).lexically_normal();
        const std::filesystem::path root = parent / ("PlutoGE-references-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root); }
        ~Scratch()
        {
            // Delete only the uniquely named test directory under the verified temp root.
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-references-"))
            {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        }
    };

    void Write(const std::filesystem::path &path, const std::string &text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        output.close();
        Require(static_cast<bool>(output), "Could not write fixture");
    }

    bool Has(const AssetReferenceScan &scan, const std::string &reference)
    {
        return std::any_of(scan.occurrences.begin(), scan.occurrences.end(),
                           [&](const auto &occurrence) { return occurrence.reference == reference; });
    }

    void NativeFormats(const std::filesystem::path &root)
    {
        const std::string reference = "project://Textures/Rough, stone; 01.png";
        Write(root / "Room.plutoscene", "PLUTOSCENE\t1\r\nENTITY\t1\t0\t1\tproject://NotAReference.png\t0,0,0\t0,0,0\t1,1,1\r\n"
            "COMPONENT\t1\tMeshComponent\t1\r\nPROPERTY\tMaterial\t2\t" + reference + "\t0\r\nEND_COMPONENT\r\n");
        auto scan = ScanAssetReferences(root / "Room.plutoscene");
        Require(scan.errors.empty() && Has(scan, reference) && scan.occurrences.size() == 1 && scan.occurrences[0].line == 4,
                "Scene field delimiters or human-facing name filtering failed");
        Write(root / "Prop.plutoprefab", "PREFAB\t4\tproject://Prefabs/Base prop.plutoprefab\t2\t1\t0\n"
            "PROPERTY\tTexture\t2\tproject://Textures\\\\Rough, stone; 01.png\t0\n");
        scan = ScanAssetReferences(root / "Prop.plutoprefab");
        Require(Has(scan, reference) && Has(scan, "project://Prefabs/Base prop.plutoprefab"), "Escaped prefab fields failed");
        Write(root / "Wall.plutomaterial", "AlbedoTexture=Textures/Rough, stone; 01.png\n"
            "NormalTexture=engine://builtin/texture/normal\nShaderGraph=project://Shaders/Lit surface.plutoshadergraph\n");
        scan = ScanAssetReferences(root / "Wall.plutomaterial");
        Require(Has(scan, reference) && Has(scan, "engine://builtin/texture/normal") && scan.occurrences.size() == 3,
                "Material-relative textures or shader references failed");
        Write(root / "Walk.plutoanimgraph", "AnimationGraphVersion=4\nState=1|Walk|Walk clip|0|0|0|1|1|project://Clips/Walk cycle.plutoclip\n");
        Require(Has(ScanAssetReferences(root / "Walk.plutoanimgraph"), "project://Clips/Walk cycle.plutoclip"), "Graph fields failed");
        Write(root / "Walk.plutoanim", "AnimationSetVersion=1\nClip=project://Clips/Walk cycle.plutoclip\n");
        Require(Has(ScanAssetReferences(root / "Walk.plutoanim"), "project://Clips/Walk cycle.plutoclip"), "Animation references failed");
        Write(root / "Data.plutoscriptable", "ScriptableObjectVersion\t1\nFIELD\tTexture\t6\t" + reference + "\n");
        Require(Has(ScanAssetReferences(root / "Data.plutoscriptable"), reference), "Scriptable fields failed");
        Write(root / "Particles.plutoparticles", "ParticleSystemVersion=2\nTexture=" + reference + "\n");
        Require(Has(ScanAssetReferences(root / "Particles.plutoparticles"), reference), "Particle references failed");
        Write(root / "Post.plutopostprocess", "PostProcessPresetVersion 1 1\nEffect \"Custom\" 1 1\nParameter 2 \"Texture\" \"" + reference + "\" 0\n");
        Require(Has(ScanAssetReferences(root / "Post.plutopostprocess"), reference), "Quoted post-process fields failed");
        Write(root / "Ui/Hud.rml", "<img src='../Textures/Rough, stone; 01.png'/><img src='../Textures/A&amp;B.png'/>\n");
        scan = ScanAssetReferences(root / "Ui/Hud.rml", {}, root);
        Require(Has(scan, reference) && Has(scan, "project://Textures/A&B.png"), "Document-relative paths failed");
        Write(root / "Models/source.gltf", "{\"images\":[{\"uri\":\"../Textures/Rough, stone; 01.png\"}]}\n");
        Require(Has(ScanAssetReferences(root / "Models/source.gltf", {}, root), reference), "glTF URI paths failed");
        Require(NormalizeAssetReference("project://Textures/../Textures/a.png") == "project://Textures/a.png", "Normalization failed");
        Require(NormalizeAssetReference("project://../outside.png").empty(), "Escaping reference accepted");
    }

    void BinaryAndLargeFiles(const std::filesystem::path &root)
    {
        const auto path = root / "Large.plutomesh";
        const std::string reference = "project://Materials/Large mesh material.plutomaterial";
        {
            std::ofstream output(path, std::ios::binary);
            output.write("LPGM", 4);
            // Put the reference beyond the old 16 MiB scanner cutoff.
            const std::string block(64 * 1024, '\0');
            for (int i = 0; i < 257; ++i) output.write(block.data(), block.size());
            const std::uint64_t size = reference.size();
            for (int i = 0; i < 8; ++i) output.put(static_cast<char>((size >> (8 * i)) & 255));
            output.write(reference.data(), reference.size());
        }
        const auto scan = ScanAssetReferences(path);
        Require(scan.errors.empty() && Has(scan, reference) && scan.occurrences.size() == 1 && scan.occurrences[0].line == 0,
                "Large binary length-prefixed reference failed");
        const auto textPath = root / "Large.plutoscene";
        {
            std::ofstream output(textPath);
            const std::string line = "PROPERTY\tHeight\t0\t" + std::string(1024, '0') + "\t0\n";
            for (int i = 0; i < 17000; ++i) output << line;
            output << "PROPERTY\tMaterial\t2\t" << reference << "\t0\n";
        }
        Require(Has(ScanAssetReferences(textPath), reference), "Large text asset was skipped");
        Write(root / "Truncated.plutomesh", std::string("LPGM") + std::string(1, 100) + std::string(7, '\0') + "project://cut");
        Require(!ScanAssetReferences(root / "Truncated.plutomesh").errors.empty(), "Truncated binary did not report an issue");
        for (const auto *extension : {".plutoscene", ".plutoprefab"})
        {
            const auto terrainPath = root / (std::string("Terrain") + extension);
            Write(terrainPath, "PROPERTY\tHeightSamples\t2\t" + std::string(3 * 1024 * 1024, '0') +
                "\t0\nPROPERTY\tMaterial\t2\t" + reference + "\t0\n");
            const auto terrain = ScanAssetReferences(terrainPath);
            Require(terrain.errors.empty() && Has(terrain, reference), "Large terrain record prevented reference scanning");
        }
        Write(root / "Oversized.plutoscene", std::string(MaxSceneRecordSize + 1, 'x') + "\nPROPERTY\tMaterial\t2\t" + reference + "\t0\n");
        const auto oversized = ScanAssetReferences(root / "Oversized.plutoscene");
        Require(!oversized.errors.empty() && Has(oversized, reference), "Oversized record did not report incomplete coverage and continue");
    }

    void InvalidationAndErrors(const std::filesystem::path &root)
    {
        std::filesystem::create_directories(root);
        const auto path = root / "Owner.plutomaterial";
        Write(path, "AlbedoTexture=A.png\n");
        AssetReferenceIndex index;
        auto result = index.Query(root, "project://A.png");
        Require(result.errors.empty() && result.owners.size() == 1 && result.owners[0].reference == "project://Owner.plutomaterial", "Initial owner query failed");
        const auto stamp = std::filesystem::last_write_time(path);
        Write(path, "AlbedoTexture=B.png\n");
        std::filesystem::last_write_time(path, stamp + std::chrono::seconds(1));
        Require(index.Query(root, "project://A.png").owners.empty(), "Edited reference stayed cached");
        Require(index.Query(root, "project://B.png").owners.size() == 1, "Updated reference missing");
        const auto moved = root / "Renamed.plutomaterial";
        std::filesystem::rename(path, moved);
        result = index.Query(root, "project://B.png");
        Require(result.owners.size() == 1 && result.owners[0].reference == "project://Renamed.plutomaterial", "Rename did not invalidate owner");
        Write(root / "Imported.plutomaterial", "AlbedoTexture=B.png\n");
        Require(index.Query(root, "project://B.png").owners.size() == 2, "Import did not appear");
        std::filesystem::remove(moved);
        Require(index.Query(root, "project://B.png").owners.size() == 1, "Deleted owner stayed cached");
        Write(root / "Longer.plutomaterial", "AlbedoTexture=B.png.backup\n");
        Require(index.Query(root, "project://B.png").owners.size() == 1, "Prefix reference matched a different asset");
        const auto imported = root / "Imported.plutomaterial";
        const auto importedStamp = std::filesystem::last_write_time(imported);
        Write(imported, "AlbedoTexture=C.png\n");
        std::filesystem::last_write_time(imported, importedStamp);
        Require(index.Query(root, "project://C.png", {}, true).owners.size() == 1, "Forced refresh reused stale stamps");
        Require(!index.Query(root / "missing", "project://C.png").errors.empty(), "Missing root did not report an issue");
        Require(!ScanAssetReferences(root / "missing.plutoscene").errors.empty(), "Missing asset did not report an issue");
        std::stop_source stop;
        stop.request_stop();
        Require(ScanAssetReferences(imported, stop.get_token()).cancelled, "File cancellation failed");
        Require(index.Query(root, "project://C.png", stop.get_token()).cancelled, "Index cancellation failed");
        for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
            Require(entry.path().extension() != ".plutometa", "Read-only lookup generated metadata");
    }
}

int main()
{
    try
    {
        Scratch scratch;
        NativeFormats(scratch.root / "Formats");
        BinaryAndLargeFiles(scratch.root / "Formats");
        InvalidationAndErrors(scratch.root / "Index");
        std::cout << "PASS: reference formats, large assets, exact matching, invalidation, errors and cancellation\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
