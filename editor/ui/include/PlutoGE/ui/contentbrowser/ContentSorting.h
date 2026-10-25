#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace PlutoGE::ui
{
    enum class ContentSortKey
    {
        Name,
        Type,
        Modified,
        Size,
    };

    struct ContentSortOrder
    {
        ContentSortKey key = ContentSortKey::Name;
        bool ascending = true;
    };

    // What sorting needs to know about a row; the panel fills one per item.
    struct ContentSortFields
    {
        bool folder = false;
        std::string_view name;
        std::string_view typeName;
        std::int64_t modified = 0; // Seconds since an arbitrary epoch; only order matters.
        std::uint64_t size = 0;
    };

    [[nodiscard]] std::string_view GetContentSortKeyName(ContentSortKey key);

    // Case-insensitive, with digit runs compared by value ("Item 2" < "Item 10").
    [[nodiscard]] int CompareNatural(std::string_view left, std::string_view right);

    // True when `left` sorts before `right`. Folders always lead; ties on the
    // chosen key fall back to the natural name so the order is stable.
    [[nodiscard]] bool ContentSortsBefore(const ContentSortFields &left, const ContentSortFields &right, ContentSortOrder order);
}
