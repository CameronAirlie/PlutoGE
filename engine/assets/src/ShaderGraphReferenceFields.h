#pragma once

#include <cstddef>
#include <string_view>

namespace PlutoGE::assets::detail
{
    enum class ReferenceFieldStatus { None, Reference, Malformed };

    // Offsets address the original line so migration changes only asset bytes.
    // Keep this grammar shared by dependency discovery and migration preparation.
    inline ReferenceFieldStatus ShaderGraphReferenceField(std::string_view line, std::size_t &offset, std::size_t &size)
    {
        const auto equals = line.find('=');
        if (equals == std::string_view::npos) return ReferenceFieldStatus::None;
        const auto key = line.substr(0, equals);
        auto value = line.substr(equals + 1);
        offset = equals + 1;
        if (key == "ShaderGraphVersion")
            return value == "1" ? ReferenceFieldStatus::None : ReferenceFieldStatus::Malformed;
        if (key == "Pass")
        {
            size = value.size();
            return value.empty() || value.find('|') != std::string_view::npos
                ? ReferenceFieldStatus::Malformed : ReferenceFieldStatus::Reference;
        }
        if (key != "TextureParameter" && key != "Node") return ReferenceFieldStatus::None;
        if (key == "Node")
        {
            const auto first = value.find('|');
            if (first == std::string_view::npos) return ReferenceFieldStatus::Malformed;
            const auto rest = value.substr(first + 1);
            const auto kind = rest.substr(0, rest.find('|'));
            if (kind != "Subgraph") return ReferenceFieldStatus::None;
        }
        const std::size_t expected = key == "Node" ? 10 : 4;
        std::string_view fields[10];
        std::size_t offsets[10]{};
        std::size_t count = 0;
        while (true)
        {
            if (count == expected) return ReferenceFieldStatus::Malformed;
            const auto end = value.find('|');
            fields[count] = value.substr(0, end);
            offsets[count++] = offset;
            if (end == std::string_view::npos) break;
            value.remove_prefix(end + 1);
            offset += end + 1;
        }
        if (key == "Node")
        {
            // Subgraphs require the asset parameter; older ordinary nodes do not.
            if (count != 10 || fields[9].empty()) return ReferenceFieldStatus::Malformed;
            offset = offsets[9]; size = fields[9].size();
        }
        else
        {
            if (count != 4 || fields[0].empty()) return ReferenceFieldStatus::Malformed;
            offset = offsets[1]; size = fields[1].size();
        }
        return ReferenceFieldStatus::Reference;
    }
}
