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
