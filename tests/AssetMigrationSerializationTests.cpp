#include <PlutoGE/assets/AssetMigrationSerialization.h>
#include <PlutoGE/assets/AssetReferences.h>

#include <iostream>
#include <span>
#include <stdexcept>

namespace
{
    using namespace PlutoGE::assets;

    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    MigrationReferenceFile Plan(const std::string &bytes)
    {
        MigrationReferenceFile plan;
        plan.reference = "project://Surface.plutoshadergraph";
        plan.contentHash = PlutoGE::content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size())));
        return plan;
    }

    void ShaderGraphMigration()
    {
        const std::string bytes = "ShaderGraphVersion=1\r\n"
            "Pass=project://Pass.plutoshadergraph\n"
            "TextureParameter=project://DisplayName|project://Pixel.png|1|0\r\n"
            "Node=1|Subgraph|project://NodeName|0,0|1,1,1,1|Color|0|0,0|0|project://Child.plutoshadergraph\n"
            "Node=2|Expression|Formula|0,0|1,1,1,1|Color|0|0,0|0|A || B\r\n"
            "Unknown=project://KeepThisExact";
        auto plan = Plan(bytes);
        plan.mappings = {{"project://Pass.plutoshadergraph", "asset://pass#0", 2},
            {"project://Pixel.png", "asset://texture#0", 3},
            {"project://Child.plutoshadergraph", "asset://child#0", 4}};
        std::string output = "prior", error = "prior";
        Require(PrepareShaderGraphReferenceMigration(plan, bytes, output, &error), error);
        const std::string expected = "ShaderGraphVersion=1\r\n"
            "Pass=asset://pass#0\n"
            "TextureParameter=project://DisplayName|asset://texture#0|1|0\r\n"
            "Node=1|Subgraph|project://NodeName|0,0|1,1,1,1|Color|0|0,0|0|asset://child#0\n"
            "Node=2|Expression|Formula|0,0|1,1,1,1|Color|0|0,0|0|A || B\r\n"
            "Unknown=project://KeepThisExact";
        Require(output == expected && error.empty(), "Graph migration changed unrelated bytes or line endings");
        const auto accepted = output;
        const auto reject = [&](const MigrationReferenceFile &invalid, const std::string &input)
        {
            Require(!PrepareShaderGraphReferenceMigration(invalid, input, output, &error) &&
                output == accepted && !error.empty(), "Rejected migration replaced previous output");
        };
        reject(plan, bytes + "stale");
        const std::string nulBytes = bytes + std::string(1, '\0');
        reject(Plan(nulBytes), nulBytes);
        auto invalid = plan; invalid.imported = true; reject(invalid, bytes);
        invalid = plan; invalid.reference = "project://Wrong.plutomaterial"; reject(invalid, bytes);
        invalid = plan; invalid.diagnostics.push_back("unresolved"); reject(invalid, bytes);
        invalid = plan; invalid.mappings.push_back(invalid.mappings.front()); reject(invalid, bytes);
        invalid = plan; invalid.mappings[0].line = 99; reject(invalid, bytes);
        invalid = plan; invalid.mappings[0].previousReference = "project://Wrong"; reject(invalid, bytes);
        invalid = plan; invalid.mappings[0].logicalReference = "asset://bad#invalid"; reject(invalid, bytes);
        invalid = plan; invalid.mappings[0] = {"project://KeepThisExact", "asset://unknown#0", 6}; reject(invalid, bytes);
        invalid = plan; invalid.mappings[0] = {"project://NodeName", "asset://name#0", 4};
        invalid.mappings.erase(invalid.mappings.begin() + 2); reject(invalid, bytes);
        for (const auto *input : {"ShaderGraphVersion=2\n", "ShaderGraphVersion=1\nShaderGraphVersion=1\n",
            "Pass=project://NoVersion\n", "ShaderGraphVersion=1\nTextureParameter=Name|project://Pixel.png|0\n",
            "ShaderGraphVersion=1\nNode=1|Subgraph|Missing parameter\n",
            "ShaderGraphVersion=1\nPass=project://Pass|extra\n"})
            reject(Plan(input), input);

        auto alias = bytes;
        Require(PrepareShaderGraphReferenceMigration(plan, alias, alias, &error) && alias == expected,
            "Aliased input/output was not prepared transactionally");

        // The existing material writer shares the bounded publication preparation.
        const std::string material = "AlbedoTexture=Pixel.png\r\nShaderGraphTexture=Detail|project://Pixel.png";
        auto materialPlan = Plan(material); materialPlan.reference = "project://Surface.plutomaterial";
        materialPlan.mappings = {{"project://Pixel.png", "asset://texture#0", 1}, {"project://Pixel.png", "asset://texture#0", 2}};
        Require(PrepareMaterialReferenceMigration(materialPlan, material, output, &error) &&
            output == "AlbedoTexture=asset://texture#0\r\nShaderGraphTexture=Detail|asset://texture#0", "Material migration regressed");
    }
}

int main()
{
    try
    {
        ShaderGraphMigration();
        std::cout << "Asset migration serialization tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
