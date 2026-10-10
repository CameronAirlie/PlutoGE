#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/assets/SceneModelInstanceRecord.h"

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

    void ManagedFields(const std::filesystem::path &root)
    {
        const auto path = root / "Managed.plutoscene";
        const std::string prefix = "SCENE\t3\nENTITY\t1\t0\t1\tScript\t0,0,0\t0,0,0\t1,1,1\nCOMPONENT\t1\tScriptComponent\t1\n";
        const std::string fields = "PROPERTY\tSource\t2\tMissing.Script\t0\n"
            "PROPERTY\t__Pluto.AssetFields.Version\t2\t1\t0\n"
            "PROPERTY\t__Pluto.AssetField.Template\t2\tPrefab\t0\n"
            "PROPERTY\tTemplate\t2\tasset://prefab-owner#0\t0\n"
            "PROPERTY\t__Pluto.AssetField.Data\t2\tScriptableObject\t0\n"
            "PROPERTY\tData\t2\tData/Settings.plutoscriptable\t0\n"
            "PROPERTY\t__Pluto.AssetField.Paint\t2\tMaterial\t0\n"
            "PROPERTY\tPaint\t2\tasset://material-owner#9007199254740993\t0\n"
            "PROPERTY\t__Pluto.AssetField.Controls\t2\tInputMapping\t0\n"
            "PROPERTY\tControls\t2\tproject://Controls.plutoinput\t0\n"
            "PROPERTY\tText\t2\tproject://Unused.plutomaterial\t0\n";
        Write(path, prefix + fields + "END_COMPONENT\n");
        auto scan = ScanAssetReferences(path, {}, root);
        Require(scan.errors.empty() && scan.occurrences.size() == 4 && !Has(scan, "project://Unused.plutomaterial") &&
            Has(scan, "project://Data/Settings.plutoscriptable") && Has(scan, "asset://material-owner#9007199254740993"),
            "Typed managed scanning lost asset fields or promoted ordinary strings");
        Require(scan.occurrences[0].expectedType == ProjectAssetType::Prefab &&
            scan.occurrences[1].expectedType == ProjectAssetType::ScriptableObject &&
            scan.occurrences[2].expectedType == ProjectAssetType::Material &&
            scan.occurrences[3].expectedType == ProjectAssetType::InputMapping,
            "Managed field roles did not survive dependency extraction");
        Write(path, "SCENE\t1\n" + prefix.substr(prefix.find('\n') + 1) + fields + "END_COMPONENT\n");
        Require(!ScanAssetReferences(path).errors.empty(), "Legacy scene accepted future managed field metadata");
        Write(path, prefix + fields + "PROPERTY\t__Pluto.AssetField.Template\t2\tPrefab\t0\nEND_COMPONENT\n");
        Require(!ScanAssetReferences(path).errors.empty(), "Duplicate managed role accepted");
        Write(path, prefix + fields);
        Require(!ScanAssetReferences(path).errors.empty(), "Unterminated managed component accepted");
        Write(path, "SCENE\t1\nCOMPONENT\t1\tScriptComponent\t1\nPROPERTY\tLegacy\t2\tasset://legacy-owner#0\t0\nEND_COMPONENT\n");
        scan = ScanAssetReferences(path);
        Require(scan.errors.empty() && Has(scan, "asset://legacy-owner#0") && scan.occurrences[0].expectedType == ProjectAssetType::Unknown,
            "Legacy missing-class fields lost conservative dependency scanning");
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
        Write(root / "Surface.plutoshadergraph", "ShaderGraphVersion=1\r\n"
            "Pass=asset://pass#0\n"
            "TextureParameter=project://NameOnly|asset://texture#0|0|1\r\n"
            "Node=1|Subgraph|project://NameOnly|0,0|1,1,1,1|Color|0|0,0|0|asset://child#0\n"
            "Node=2|TextureSample|Sample|0,0|1,1,1,1|Color|0|0,0|0|project://NameOnly\n"
            "Node=3|Expression|Formula|0,0|1,1,1,1|Color|0|0,0|0|A || B\n"
            "Variable=project://NameOnly|0|0,0,0,0\nUnknown=project://NameOnly\n");
        scan = ScanAssetReferences(root / "Surface.plutoshadergraph");
        Require(scan.errors.empty() && scan.occurrences.size() == 3 &&
            Has(scan, "asset://pass#0") && Has(scan, "asset://texture#0") && Has(scan, "asset://child#0") &&
            scan.occurrences[0].line == 2 && scan.occurrences[1].line == 3 && scan.occurrences[2].line == 4,
            "Shader graph scan confused names or expressions with reference fields");
        for (const auto *field : {"ShaderGraphVersion=2", "TextureParameter=Name|asset://texture#0|0", "Node=1|Subgraph|NoParameter",
            "Pass=asset://pass#0|unexpected", "TextureParameter=Name|asset://bad#invalid|0|0"})
        {
            Write(root / "Malformed.plutoshadergraph", std::string("ShaderGraphVersion=1\n") + field + "\n");
            Require(!ScanAssetReferences(root / "Malformed.plutoshadergraph").errors.empty(),
                "Malformed shader graph dependency silently accepted");
        }
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
        Write(root / "Ui/Hud.rml", "<img src='Textures/Rough, stone; 01.png'/><img src='Textures/A&amp;B.png'/>"
            "<link href='../Textures/Style.rcss'/>\n");
        scan = ScanAssetReferences(root / "Ui/Hud.rml", {}, root);
        Require(Has(scan, reference) && Has(scan, "project://Textures/A&B.png") &&
            Has(scan, "project://Textures/Style.rcss"), "RmlUi image and stylesheet paths failed");
        Write(root / "Models/source.gltf", "{\"images\":[{\"uri\":\"../Textures/Rough, stone; 01.png\"}]}\n");
        Require(Has(ScanAssetReferences(root / "Models/source.gltf", {}, root), reference), "glTF URI paths failed");
        Write(root / "Models/encoded.gltf", "{\"buffers\":[{\"uri\":\"geometry%20data%2B100%25.bin\"}],\"images\":[{\"uri\":\"A&amp;B.png\"}]}\n");
        scan = ScanAssetReferences(root / "Models/encoded.gltf", {}, root);
        Require(scan.errors.empty() && Has(scan, "project://Models/geometry data+100%.bin") && Has(scan, "project://Models/A&amp;B.png"),
                "glTF URI percent decoding or literal ampersand handling failed");
        Write(root / "Models/uppercase.GLTF", "{\"buffers\":[{\"uri\":\"geometry%20data.bin\"}]}\n");
        Require(Has(ScanAssetReferences(root / "Models/uppercase.GLTF", {}, root), "project://Models/geometry data.bin"),
                "Uppercase glTF extension skipped URI decoding");
        for (const auto *uri : {"bad%GG.bin", "bad%00.bin", "../../outside.bin"})
        {
            Write(root / "Models/invalid.gltf", std::string("{\"buffers\":[{\"uri\":\"") + uri + "\"}]}\n");
            Require(!ScanAssetReferences(root / "Models/invalid.gltf", {}, root).errors.empty(), "Invalid glTF URI was silently ignored");
        }
        Require(NormalizeAssetReference("project://Textures/../Textures/a.png") == "project://Textures/a.png", "Normalization failed");
        Require(NormalizeAssetReference("project://../outside.png").empty(), "Escaping reference accepted");
        Write(root / "Logical.plutomaterial", "AlbedoTexture=asset://texture-owner#0\n");
        Require(Has(ScanAssetReferences(root / "Logical.plutomaterial"), "asset://texture-owner#0"), "Logical material dependency omitted");
        Write(root / "Logical.plutomaterial", "AlbedoTexture=asset://texture-owner#bad\n");
        Require(!ScanAssetReferences(root / "Logical.plutomaterial").errors.empty(), "Malformed logical dependency silently ignored");
        const std::string logical = "asset://model-owner#42";
        std::string binary = "LPGM";
        for (int index = 0; index < 8; ++index) binary += static_cast<char>((static_cast<std::uint64_t>(logical.size()) >> (8 * index)) & 255);
        binary += logical;
        Write(root / "Logical.plutomesh", binary);
        Require(Has(ScanAssetReferences(root / "Logical.plutomesh"), logical), "Binary logical dependency omitted");
        for (const std::uint32_t version : {4u, 5u, 6u})
        {
            std::string mesh = "LPGM";
            for (std::size_t index = 0; index < 4; ++index) mesh += static_cast<char>((version >> (8 * index)) & 255);
            auto pod = [&](std::uint64_t value) { for (std::size_t index = 0; index < 8; ++index) mesh += static_cast<char>((value >> (8 * index)) & 255); };
            auto string = [&](const std::string &text) { pod(text.size()); mesh += text; };
            string(logical);
            string("project://Models/Source.gltf");
            if (version >= 5) { string("model-owner"); pod(42); }
            mesh += std::string(3, '\1');
            Write(root / "Provenance.plutomesh", mesh);
            scan = ScanAssetReferences(root / "Provenance.plutomesh");
            Require(scan.errors.empty() && scan.occurrences.size() == 2 && scan.occurrences.front().role == AssetReferenceRole::Runtime &&
                    scan.occurrences.back().role == (version <= 5 ? AssetReferenceRole::ImportSource : AssetReferenceRole::Runtime),
                    "Mesh source provenance lost its role or classified an unsupported future trailer");
        }
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

    void LinkedModelScopes(const std::filesystem::path &root)
    {
        StaticModelInstanceState state;
        state.rootEntityId = 1; state.artifactGenerationKey[0] = 1;
        state.packageArtifact = {"project://Models/Accepted.plutomodel", {}}; state.packageArtifact.digest[0] = 2;
        state.accepted.layout.sourceAssetId = "source"; state.accepted.layout.meshReference = "asset://source#2";
        state.accepted.layout.hierarchyDigest[0] = 3; state.accepted.meshDigest[0] = 4;
        state.accepted.submeshCount = 1; state.accepted.materialSlotCount = 1;
        state.accepted.layout.nodes = {{1ull << 45, "Source", -1, glm::mat4(1)}};
        state.accepted.layout.bindings = {{0, 0, glm::mat4(1)}};
        state.defaultMaterials = {"asset://source#3"};
        state.overrides.hierarchyDigest = state.accepted.layout.hierarchyDigest; state.overrides.meshDigest = state.accepted.meshDigest;
        state.overrides.materials = {{0, 0, "asset://external#7"}};
        state.nodeEntities = {{1ull << 45, 2}}; state.bindingEntities = {3};
        std::string record, error;
        Require(SerializeSceneModelInstanceRecord(state, record, &error), error);
        const auto path = root / "Linked.plutoscene";
        const std::string entities = "ENTITY\t1\t0\t1\tRoot\t0,0,0\t0,0,0\t1,1,1\n"
            "ENTITY\t2\t1\t1\tSource\t0,0,0\t0,0,0\t1,1,1\n"
            "ENTITY\t3\t2\t1\tGeometry 0\t0,0,0\t0,0,0\t1,1,1\n"
            "COMPONENT\t3\tMeshComponent\t1\nPROPERTY\tMeshAssetReference\t2\tasset://source#2\t0\n"
            "PROPERTY\tSubmeshIndex\t1\t0\t0\nPROPERTY\tSubmeshCount\t1\t1\t0\n"
            "PROPERTY\tSubmeshOverrides.0.AlbedoPath\t2\tasset://source#4\t0\n"
            "PROPERTY\tSubmeshOverrides.0.MaterialAsset\t2\tasset://external#7\t0\nEND_COMPONENT\n";
        Write(path, "SCENE\t3\n" + record + "\n" + entities);
        const auto scan = ScanAssetReferences(path);
        Require(scan.errors.empty() && scan.modelInstances.size() == 1 && scan.modelInstances.front()->nodeEntities.front().sourceNodeId == (1ull << 45),
            "Linked dependency scan lost accepted source evidence");
        const auto hasRole = [&](const std::string &reference, AssetReferenceRole role)
        { return std::any_of(scan.occurrences.begin(), scan.occurrences.end(), [&](const auto &occurrence) { return occurrence.reference == reference && occurrence.role == role; }); };
        Require(hasRole("asset://source#0", AssetReferenceRole::ImportSource) &&
            hasRole("asset://source#2", AssetReferenceRole::AcceptedGeneration) &&
            hasRole("asset://source#4", AssetReferenceRole::AcceptedGeneration) &&
            hasRole("asset://external#7", AssetReferenceRole::Runtime) &&
            !hasRole("asset://source#2", AssetReferenceRole::Runtime), "Private generation dependencies leaked into the current catalog traversal");
        Write(path, "SCENE\t3\n" + record + "\n" + record + "\n" + entities);
        Require(!ScanAssetReferences(path).errors.empty(), "Duplicate generated ownership was scanned as complete");
        Write(path, "SCENE\t3\n" + entities + record + "\n");
        Require(!ScanAssetReferences(path).errors.empty(), "Late linked model record was scanned as complete");
        auto missing = entities;
        missing.erase(0, missing.find('\n') + 1);
        Write(path, "SCENE\t3\n" + record + "\n" + missing);
        Require(!ScanAssetReferences(path).errors.empty(), "Missing linked root was scanned as complete");
        auto overflowing = entities;
        overflowing.replace(overflowing.find("ENTITY\t1\t"), 8, "ENTITY\t4294967297\t");
        Write(path, "SCENE\t3\n" + record + "\n" + overflowing);
        Require(!ScanAssetReferences(path).errors.empty(), "Overflowing linked entity ID was truncated");
        std::filesystem::remove(path);
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
        ManagedFields(scratch.root / "Managed");
        BinaryAndLargeFiles(scratch.root / "Formats");
        LinkedModelScopes(scratch.root / "Formats");
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
