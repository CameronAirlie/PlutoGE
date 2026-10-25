#include "PlutoGE/ui/contentbrowser/ContentSorting.h"

#include <cctype>

namespace PlutoGE::ui
{
    std::string_view GetContentSortKeyName(ContentSortKey key)
    {
        switch (key)
        {
        case ContentSortKey::Name: return "Name";
        case ContentSortKey::Type: return "Type";
        case ContentSortKey::Modified: return "Date Modified";
        case ContentSortKey::Size: return "Size";
        }
        return "Name";
    }

    int CompareNatural(std::string_view left, std::string_view right)
    {
        const auto isDigit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < left.size() && j < right.size())
        {
            if (isDigit(left[i]) && isDigit(right[j]))
            {
                // Compare digit runs by value: skip leading zeros, then length, then digits.
                const auto runStart = [&](std::string_view text, std::size_t &k)
                {
                    while (k < text.size() && text[k] == '0') ++k;
                    const auto start = k;
                    while (k < text.size() && isDigit(text[k])) ++k;
                    return text.substr(start, k - start);
                };
                const auto a = runStart(left, i);
                const auto b = runStart(right, j);
                if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
                if (const int digits = a.compare(b); digits != 0) return digits < 0 ? -1 : 1;
                continue;
            }
            const int a = std::tolower(static_cast<unsigned char>(left[i]));
            const int b = std::tolower(static_cast<unsigned char>(right[j]));
            if (a != b) return a < b ? -1 : 1;
            ++i;
            ++j;
        }
        if (i < left.size()) return 1;
        if (j < right.size()) return -1;
        // Equal ignoring case and zero padding: fall back to an exact comparison for stability.
        const int exact = left.compare(right);
        return exact < 0 ? -1 : exact > 0 ? 1 : 0;
    }

    bool ContentSortsBefore(const ContentSortFields &left, const ContentSortFields &right, ContentSortOrder order)
    {
        if (left.folder != right.folder)
        {
            return left.folder;
        }

        int result = 0;
        switch (order.key)
        {
        case ContentSortKey::Name:
            break;
        case ContentSortKey::Type:
            result = CompareNatural(left.typeName, right.typeName);
            break;
        case ContentSortKey::Modified:
            result = left.modified < right.modified ? -1 : left.modified > right.modified ? 1 : 0;
            break;
        case ContentSortKey::Size:
            result = left.size < right.size ? -1 : left.size > right.size ? 1 : 0;
            break;
        }
        if (result == 0)
        {
            result = CompareNatural(left.name, right.name);
        }
        return order.ascending ? result < 0 : result > 0;
    }
}
