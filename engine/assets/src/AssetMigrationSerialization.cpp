#include "PlutoGE/assets/AssetMigrationSerialization.h"
#include "PlutoGE/assets/AssetReference.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "ShaderGraphReferenceFields.h"
#include <map>
#include <span>
#include <exception>

namespace PlutoGE::assets
{
    namespace
    {
        bool PrepareReferenceMigration(bool shaderGraph, const MigrationReferenceFile &plan,
            std::string_view input, std::string &output, std::string *errorMessage)
        {
            auto fail = [&](const std::string &message) { if (errorMessage) *errorMessage = message; return false; };
            try
            {
                constexpr std::size_t limit = 16 * 1024 * 1024;
                const auto extension = std::filesystem::path(plan.reference).extension();
                if (shaderGraph ? extension != ".plutoshadergraph" : (extension != ".plutomaterial" && extension != ".mat"))
                    return fail("Unsupported reference migration format.");
                if (plan.imported || !plan.diagnostics.empty()) return fail("Imported or unresolved asset requires reimport or review.");
                if (input.size() > limit || input.find('\0') != std::string_view::npos ||
                    content::HashContent(std::as_bytes(std::span(input.data(), input.size()))) != plan.contentHash)
                    return fail("Reference migration input differs from its audited bytes or exceeds format limits.");
                std::map<std::size_t, const MigrationReferenceMapping *> mappings;
                for (const auto &mapping : plan.mappings)
                {
                    AssetReference identity;
                    if (!mapping.line || !ParseAssetReference(mapping.logicalReference, identity) ||
                        mapping.logicalReference.find_first_of("|\r\n") != std::string::npos ||
                        NormalizeAssetReference(mapping.previousReference).empty() ||
                        !mappings.emplace(mapping.line, &mapping).second)
                        return fail("Invalid or duplicate reference occurrence mapping.");
                }
                std::string candidate;
                candidate.reserve(input.size());
                std::size_t offset = 0, lineNumber = 1, applied = 0, versionRecords = 0;
                while (offset < input.size())
                {
                    const auto end = input.find('\n', offset);
                    const auto length = (end == std::string_view::npos ? input.size() : end + 1) - offset;
                    const auto raw = input.substr(offset, length);
                    auto line = raw;
                    if (line.ends_with('\n')) line.remove_suffix(1);
                    if (line.ends_with('\r')) line.remove_suffix(1);
                    if (shaderGraph)
                    {
                        if (line.starts_with("ShaderGraphVersion="))
                        {
                            if (line != "ShaderGraphVersion=1" || ++versionRecords != 1)
                                return fail("Unsupported or duplicate shader graph version.");
                        }
                        std::size_t fieldOffset = 0, fieldSize = 0;
                        if (detail::ShaderGraphReferenceField(line, fieldOffset, fieldSize) == detail::ReferenceFieldStatus::Malformed)
                            return fail("Malformed shader graph dependency field.");
                    }
                    const auto found = mappings.find(lineNumber);
                    if (found == mappings.end()) candidate.append(raw);
                    else
                    {
                        const auto equals = line.find('=');
                        if (equals == std::string_view::npos) return fail("Mapped reference occurrence is not a field.");
                        const auto key = line.substr(0, equals);
                        auto value = line.substr(equals + 1);
                        std::size_t valueStart = equals + 1;
                        const bool texture = key == "AlbedoTexture" || key == "NormalTexture" ||
                            key == "MetallicTexture" || key == "RoughnessTexture" || key == "EmissionTexture";
                        if (shaderGraph)
                        {
                            std::size_t valueSize = 0;
                            if (detail::ShaderGraphReferenceField(line, valueStart, valueSize) != detail::ReferenceFieldStatus::Reference)
                                return fail("Mapping targets an unsupported or malformed shader graph field.");
                            value = line.substr(valueStart, valueSize);
                        }
                        else if (key == "ShaderGraphTexture")
                        {
                            const auto delimiter = value.find('|');
                            if (delimiter == std::string_view::npos || delimiter == 0 ||
                                value.find('|', delimiter + 1) != std::string_view::npos)
                                return fail("Malformed shader graph texture field.");
                            valueStart += delimiter + 1; value.remove_prefix(delimiter + 1);
                        }
                        else if (!texture && key != "ShaderGraph") return fail("Mapping targets an unsupported material field.");
                        auto reference = NormalizeAssetReference(value);
                        if (reference.empty() && !shaderGraph && texture && !value.empty() && value.find("://") == std::string_view::npos &&
                            !std::filesystem::path(value).is_absolute())
                            reference = NormalizeAssetReference("project://" + std::string(value));
                        if (reference != found->second->previousReference) return fail("Reference occurrence differs from its audited mapping.");
                        candidate.append(line.substr(0, valueStart));
                        candidate.append(found->second->logicalReference);
                        candidate.append(raw.substr(valueStart + value.size()));
                        ++applied;
                    }
                    if (candidate.size() > limit) return fail("Converted asset exceeds format limits.");
                    offset += length; ++lineNumber;
                }
                if (shaderGraph && versionRecords != 1) return fail("Missing shader graph version.");
                if (applied != mappings.size()) return fail("Reference mapping points outside the input.");
                output = std::move(candidate);
                if (errorMessage) errorMessage->clear();
                return true;
            }
            catch (const std::exception &error) { return fail(std::string("Cannot prepare reference migration: ") + error.what()); }
        }
    }

    bool PrepareMaterialReferenceMigration(const MigrationReferenceFile &plan,
        std::string_view input, std::string &output, std::string *errorMessage)
    {
        return PrepareReferenceMigration(false, plan, input, output, errorMessage);
    }

    bool PrepareShaderGraphReferenceMigration(const MigrationReferenceFile &plan,
        std::string_view input, std::string &output, std::string *errorMessage)
    {
        return PrepareReferenceMigration(true, plan, input, output, errorMessage);
    }
}
