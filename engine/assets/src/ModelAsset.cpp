#include "PlutoGE/assets/ModelAsset.h"

#include <fstream>
#include <sstream>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view kHeader = "PLUTOMODEL\t1";

        void SetError(std::string *output, std::string message)
        {
            if (output) *output = std::move(message);
        }
    }

    std::uint64_t MakeModelSubAssetId(ProjectAssetType type, std::string_view name)
    {
        std::uint64_t hash = 14695981039346656037ull;
        const auto typeValue = static_cast<std::uint32_t>(type);
        for (std::size_t byte = 0; byte < sizeof(typeValue); ++byte)
        {
            hash ^= static_cast<unsigned char>((typeValue >> (byte * 8)) & 0xffu);
            hash *= 1099511628211ull;
        }
        for (const unsigned char character : name)
        {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        return hash == 0 ? 1 : hash;
    }

    bool SaveModelAsset(const std::string &path, const ModelAsset &asset, std::string *errorMessage)
    {
        std::ofstream output(path, std::ios::trunc);
        if (!output)
        {
            SetError(errorMessage, "Failed to create model asset: " + path);
            return false;
        }
        output << kHeader << '\n'
               << "SOURCE\t" << asset.sourceReference << '\n'
               << "SOURCE_ID\t" << asset.sourceAssetId << '\n'
               << "SOURCE_HASH\t" << asset.sourceContentHash << '\n'
               << "IMPORTER_VERSION\t" << asset.importerVersion << '\n';
        for (const auto &object : asset.objects)
        {
            output << "OBJECT\t" << object.localId << '\t'
                   << Project::GetAssetTypeName(object.type) << '\t'
                   << object.name << '\t' << object.reference << '\n';
        }
        return output.good();
    }

    bool LoadModelAsset(const std::string &path, ModelAsset &asset, std::string *errorMessage)
    {
        std::ifstream input(path);
        std::string line;
        if (!input || !std::getline(input, line) || line != kHeader)
        {
            SetError(errorMessage, "Invalid model asset: " + path);
            return false;
        }
        asset = {};
        while (std::getline(input, line))
        {
            std::vector<std::string> fields;
            std::stringstream stream(line);
            std::string field;
            while (std::getline(stream, field, '\t')) fields.push_back(std::move(field));
            if (fields.size() >= 2 && fields[0] == "SOURCE") asset.sourceReference = fields[1];
            else if (fields.size() >= 2 && fields[0] == "SOURCE_ID") asset.sourceAssetId = fields[1];
            else if (fields.size() >= 2 && fields[0] == "SOURCE_HASH")
            {
                try { asset.sourceContentHash = std::stoull(fields[1]); } catch (...) {}
            }
            else if (fields.size() >= 2 && fields[0] == "IMPORTER_VERSION")
            {
                try { asset.importerVersion = static_cast<std::uint32_t>(std::stoul(fields[1])); } catch (...) {}
            }
            else if (fields.size() >= 5 && fields[0] == "OBJECT")
            {
                ModelSubAsset object;
                try { object.localId = std::stoull(fields[1]); } catch (...) { continue; }
                object.type = Project::ParseAssetTypeName(fields[2]);
                object.name = fields[3];
                object.reference = fields[4];
                asset.objects.push_back(std::move(object));
            }
        }
        return true;
    }
}
