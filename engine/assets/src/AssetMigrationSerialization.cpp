#include "PlutoGE/assets/AssetMigrationSerialization.h"
#include "PlutoGE/assets/AssetReference.h"
#include "PlutoGE/assets/AssetReferences.h"
#include <map>
#include <span>
#include <exception>

namespace PlutoGE::assets
{
    bool PrepareMaterialReferenceMigration(const MigrationReferenceFile &plan,
        std::string_view input, std::string &output, std::string *errorMessage)
    {
        auto fail = [&](const std::string &message) { if (errorMessage) *errorMessage = message; return false; };
        try
        {
            constexpr std::size_t limit = 16 * 1024 * 1024;
            const auto extension = std::filesystem::path(plan.reference).extension();
            if (extension != ".plutomaterial" && extension != ".mat") return fail("Unsupported material migration format.");
            if (plan.imported || !plan.diagnostics.empty()) return fail("Imported or unresolved material requires reimport or review.");
            if (input.size() > limit || input.find('\0') != std::string_view::npos ||
                content::HashContent(std::as_bytes(std::span(input.data(), input.size()))) != plan.contentHash)
                return fail("Material migration input differs from its audited bytes or exceeds format limits.");
            std::map<std::size_t, const MigrationReferenceMapping *> mappings;
            for (const auto &mapping : plan.mappings)
            {
                AssetReference identity;
                if (!mapping.line || !ParseAssetReference(mapping.logicalReference, identity) ||
                    mapping.logicalReference.find_first_of("|\r\n") != std::string::npos ||
                    NormalizeAssetReference(mapping.previousReference).empty() ||
                    !mappings.emplace(mapping.line, &mapping).second)
                    return fail("Invalid or duplicate material occurrence mapping.");
            }
            std::string candidate;
            candidate.reserve(input.size());
            std::size_t offset = 0, lineNumber = 1, applied = 0;
            while (offset < input.size())
            {
                const auto end = input.find('\n', offset);
                const auto length = (end == std::string_view::npos ? input.size() : end + 1) - offset;
                const auto raw = input.substr(offset, length);
                auto line = raw;
                if (line.ends_with('\n')) line.remove_suffix(1);
                if (line.ends_with('\r')) line.remove_suffix(1);
                const auto found = mappings.find(lineNumber);
                if (found == mappings.end()) candidate.append(raw);
                else
                {
                    const auto equals = line.find('=');
                    if (equals == std::string_view::npos) return fail("Mapped material occurrence is not a field.");
                    const auto key = line.substr(0, equals);
                    auto value = line.substr(equals + 1);
                    std::size_t valueStart = equals + 1;
                    const bool texture = key == "AlbedoTexture" || key == "NormalTexture" ||
                        key == "MetallicTexture" || key == "RoughnessTexture" || key == "EmissionTexture";
                    if (key == "ShaderGraphTexture")
                    {
                        const auto delimiter = value.find('|');
                        if (delimiter == std::string_view::npos || delimiter == 0 ||
                            value.find('|', delimiter + 1) != std::string_view::npos)
                            return fail("Malformed shader graph texture field.");
                        valueStart += delimiter + 1; value.remove_prefix(delimiter + 1);
                    }
                    else if (!texture && key != "ShaderGraph") return fail("Mapping targets an unsupported material field.");
                    auto reference = NormalizeAssetReference(value);
                    if (reference.empty() && texture && !value.empty() && value.find("://") == std::string_view::npos &&
                        !std::filesystem::path(value).is_absolute())
                        reference = NormalizeAssetReference("project://" + std::string(value));
                    if (reference != found->second->previousReference) return fail("Material occurrence differs from its audited mapping.");
                    candidate.append(line.substr(0, valueStart));
                    candidate.append(found->second->logicalReference);
                    candidate.append(raw.substr(line.size()));
                    ++applied;
                }
                if (candidate.size() > limit) return fail("Converted material exceeds format limits.");
                offset += length; ++lineNumber;
            }
            if (applied != mappings.size()) return fail("Material mapping points outside the input.");
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error) { return fail(std::string("Cannot prepare material migration: ") + error.what()); }
    }
}
