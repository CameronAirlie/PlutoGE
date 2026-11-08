#include "PlutoGE/asset_import/ModelObjectNaming.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace PlutoGE::assetimport
{
    namespace
    {
        std::string Lower(std::string_view text)
        {
            std::string lowered(text);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return lowered;
        }

        bool IsReservedDeviceName(std::string_view name)
        {
            static constexpr std::array<std::string_view, 22> kReserved{
                "con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
            const auto stem = Lower(name.substr(0, name.find('.')));
            return std::find(kReserved.begin(), kReserved.end(), stem) != kReserved.end();
        }
    }

    std::string SanitizeModelObjectName(std::string_view name)
    {
        std::string sanitized;
        sanitized.reserve(name.size());
        for (const char character : name)
        {
            const auto code = static_cast<unsigned char>(character);
            const bool invalid = code < 0x20 || code == 0x7f || std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos;
            sanitized.push_back(invalid ? '_' : character);
        }
        const auto first = sanitized.find_first_not_of(' ');
        if (first == std::string::npos) return {};
        sanitized.erase(0, first);
        while (!sanitized.empty() && (sanitized.back() == ' ' || sanitized.back() == '.')) sanitized.pop_back();
        if (sanitized.empty()) return {};
        if (IsReservedDeviceName(sanitized)) sanitized += '_';
        return sanitized;
    }

    std::string ModelObjectNameAllocator::Allocate(std::string_view authoredName, std::string_view fallbackName)
    {
        auto base = SanitizeModelObjectName(authoredName);
        if (base.empty()) base = SanitizeModelObjectName(fallbackName);
        auto candidate = base;
        for (std::size_t suffix = 1; !m_used.insert(Lower(candidate)).second; ++suffix)
            candidate = base + "_" + std::to_string(suffix);
        return candidate;
    }

    std::string ModelMaterialSourceKey(std::string_view allocatedName, bool authoredName, std::size_t legacySlot)
    {
        return authoredName ? "material/name/" + std::string(allocatedName) : LegacyModelMaterialSourceKey(legacySlot);
    }

    std::string LegacyModelMaterialSourceKey(std::size_t legacySlot)
    {
        return "material/slot/" + std::to_string(legacySlot);
    }

    std::vector<std::size_t> LegacyModelMaterialSlots(const std::vector<ImportedMaterialData> &materials)
    {
        // Slots are deduplicated by name and parameters, preserving source order, so merging by parameters alone
        // reproduces the slots of earlier imports.
        std::vector<std::size_t> slots;
        std::vector<const ImportedMaterialData *> legacy;
        slots.reserve(materials.size());
        for (const auto &material : materials)
        {
            const auto match = std::find_if(legacy.begin(), legacy.end(),
                [&](const ImportedMaterialData *candidate) { return ImportedMaterialParametersEqual(*candidate, material); });
            slots.push_back(static_cast<std::size_t>(std::distance(legacy.begin(), match)));
            if (match == legacy.end()) legacy.push_back(&material);
        }
        return slots;
    }
}
