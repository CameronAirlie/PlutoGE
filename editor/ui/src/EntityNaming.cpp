#include "PlutoGE/ui/EntityNaming.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace PlutoGE::ui
{
    namespace
    {
        // Strips " (N)" and any number of trailing " Copy" from a name.
        std::string_view BaseName(std::string_view name)
        {
            for (;;)
            {
                if (name.ends_with(" Copy"))
                {
                    name.remove_suffix(5);
                    continue;
                }
                if (name.size() >= 4 && name.back() == ')')
                {
                    const auto open = name.rfind(" (");
                    if (open != std::string_view::npos && open + 2 < name.size() - 1)
                    {
                        const auto digits = name.substr(open + 2, name.size() - open - 3);
                        if (std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
                        {
                            name = name.substr(0, open);
                            continue;
                        }
                    }
                }
                return name;
            }
        }
    }

    std::string NicifyFieldName(std::string_view identifier)
    {
        if (identifier.starts_with("m_"))
            identifier.remove_prefix(2);
        while (!identifier.empty() && identifier.front() == '_')
            identifier.remove_prefix(1);

        std::string label;
        label.reserve(identifier.size() + 4);
        const auto at = [&](std::size_t index) { return index < identifier.size() ? static_cast<unsigned char>(identifier[index]) : 0; };
        for (std::size_t i = 0; i < identifier.size(); ++i)
        {
            const unsigned char c = at(i);
            if (c == '_')
            {
                if (!label.empty() && label.back() != ' ')
                    label += ' ';
                continue;
            }
            if (i > 0 && !label.empty() && label.back() != ' ')
            {
                const unsigned char previous = at(i - 1);
                const bool upper = std::isupper(c) != 0;
                const bool digit = std::isdigit(c) != 0;
                // "moveSpeed" -> "move Speed"; "HUDScale" -> "HUD Scale"; "jump2" -> "jump 2".
                const bool wordStart = (upper && (std::islower(previous) || std::isdigit(previous))) ||
                                       (upper && std::isupper(previous) && std::islower(at(i + 1))) ||
                                       (digit && std::isalpha(previous)) || (std::isalpha(c) && std::isdigit(previous));
                if (wordStart)
                    label += ' ';
            }
            label += static_cast<char>(c);
        }
        if (!label.empty())
            label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
        return label.empty() ? std::string(identifier) : label;
    }

    std::string NextDuplicateName(std::string_view name, std::span<const std::string> siblingNames)
    {
        const std::string base(BaseName(name));
        const std::unordered_set<std::string_view> taken(siblingNames.begin(), siblingNames.end());
        for (int number = 1;; ++number)
        {
            std::string candidate = base + " (" + std::to_string(number) + ")";
            if (!taken.contains(candidate))
                return candidate;
        }
    }
}
