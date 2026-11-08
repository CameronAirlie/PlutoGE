#pragma once

#include "PlutoGE/import/MeshImporter.h"

#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::assetimport
{
    // Turns an authored name into one usable as a file stem on every platform: path separators, reserved and control
    // characters become '_', trailing dots and spaces are dropped and reserved device names are suffixed. Returns an
    // empty string when nothing usable remains.
    [[nodiscard]] std::string SanitizeModelObjectName(std::string_view name);

    // Allocates the display/file names of one model import's objects of one kind. Names are compared without case,
    // as the files they become share a directory on case-insensitive file systems; later duplicates gain "_1", "_2"...
    class ModelObjectNameAllocator
    {
    public:
        // The sanitized authored name, or the fallback when the source authored none.
        std::string Allocate(std::string_view authoredName, std::string_view fallbackName);

    private:
        std::set<std::string> m_used;
    };

    // The identity keys of a model's material slots (see ResolveModelObjectId). Named materials are keyed by name, so a
    // slot keeps its overrides when other materials are added, removed or reordered in the source; unnamed materials
    // fall back to their legacy slot.
    [[nodiscard]] std::string ModelMaterialSourceKey(std::string_view allocatedName, bool authoredName, std::size_t legacySlot);
    [[nodiscard]] std::string LegacyModelMaterialSourceKey(std::size_t legacySlot);

    // For each material, the slot it occupied when imports merged every material with equal parameters regardless of
    // name. Identities recorded by those imports are keyed by that slot, so name-keyed materials can adopt them.
    [[nodiscard]] std::vector<std::size_t> LegacyModelMaterialSlots(const std::vector<ImportedMaterialData> &materials);
}
