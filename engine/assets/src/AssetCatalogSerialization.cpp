#include "PlutoGE/assets/AssetCatalogSerialization.h"

#include <iomanip>
#include <locale>
#include <sstream>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::size_t MaximumCatalogBytes = 16 * 1024 * 1024;
        bool LineText(std::string_view text)
        {
            return text.find_first_of("\r\n") == std::string_view::npos && text.find('\0') == std::string_view::npos;
        }
        bool Fail(std::string *error, const char *message)
        {
            if (error) *error = message;
            return false;
        }
    }

    bool SerializeAssetCatalog(const AssetCatalog &catalog, std::string &text, std::string *errorMessage)
    {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << "PLUTOCATALOG\t1\n";
        for (const auto &object : catalog.GetObjects())
        {
            std::string identity;
            if (!SerializeAssetReference(object.identity, identity) || !LineText(object.name) || !LineText(object.location) ||
                object.location.starts_with("asset://") || object.type < ProjectAssetType::Unknown || object.type >= ProjectAssetType::Count ||
                object.ownership < AssetOwnership::Unclassified || object.ownership > AssetOwnership::Imported)
                return Fail(errorMessage, "Catalog contains an invalid object descriptor.");
            output << "OBJECT " << std::quoted(identity) << ' ' << static_cast<unsigned>(object.type) << ' '
                   << static_cast<unsigned>(object.ownership) << ' ' << std::quoted(object.name) << ' ' << std::quoted(object.location) << '\n';
            if (output.tellp() > MaximumCatalogBytes) return Fail(errorMessage, "Catalog exceeds the supported size limit.");
        }
        text = output.str();
        if (errorMessage) errorMessage->clear();
        return true;
    }

    bool ParseAssetCatalog(std::string_view text, AssetCatalog &catalog, std::string *errorMessage)
    {
        if (text.size() > MaximumCatalogBytes || text.find('\0') != std::string_view::npos)
            return Fail(errorMessage, "Invalid or oversized asset catalog.");
        std::istringstream input{std::string(text)};
        std::string line;
        auto next = [&]()
        {
            if (!std::getline(input, line)) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        };
        if (!next() || line != "PLUTOCATALOG\t1") return Fail(errorMessage, "Unsupported asset catalog schema.");
        std::vector<AssetObjectDescriptor> objects;
        while (next())
        {
            std::istringstream record(line);
            record.imbue(std::locale::classic());
            std::string tag, identity;
            unsigned type = 0, ownership = 0;
            AssetObjectDescriptor object;
            if (!(record >> tag >> std::quoted(identity) >> type >> ownership >> std::quoted(object.name) >> std::quoted(object.location)) ||
                tag != "OBJECT" || type >= static_cast<unsigned>(ProjectAssetType::Count) || ownership > static_cast<unsigned>(AssetOwnership::Imported) ||
                !ParseAssetReference(identity, object.identity) || object.identity.IsEmpty() || !LineText(object.name) || !LineText(object.location) ||
                object.location.starts_with("asset://"))
                return Fail(errorMessage, "Invalid asset catalog object record.");
            record >> std::ws;
            if (!record.eof()) return Fail(errorMessage, "Unexpected asset catalog fields.");
            object.type = static_cast<ProjectAssetType>(type);
            object.ownership = static_cast<AssetOwnership>(ownership);
            objects.push_back(std::move(object));
        }
        if (!input.eof()) return Fail(errorMessage, "Cannot read asset catalog.");
        return catalog.Replace(std::move(objects), errorMessage);
    }
}
