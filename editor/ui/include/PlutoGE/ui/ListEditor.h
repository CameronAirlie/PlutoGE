#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <vector>

namespace PlutoGE::ui
{
    struct ListEditorOptions
    {
        const char *addLabel = "Add";
        const char *emptyLabel = "Empty";
        bool reorderable = true;
        bool defaultOpen = true;
    };

    // The list being edited, described by its size and edit callbacks so any
    // container and element type can be shown. Structural edits (add, remove,
    // move) are requested after every row has been drawn, never mid-iteration.
    struct ListEditorModel
    {
        std::size_t count = 0;
        // Draws element `index` within the row's remaining width; true when it changed the element.
        std::function<bool(std::size_t index)> renderElement;
        std::function<void()> add;
        std::function<void(std::size_t index)> remove;
        // Moves the element at `from` so it ends up at index `to`.
        std::function<void(std::size_t from, std::size_t to)> move;
    };

    // A collapsible list: a header with the element count, one row per element
    // with a drag handle for reordering and a remove button, then an add button.
    // Returns true when the list or any element changed.
    bool RenderListEditor(const char *label, const ListEditorModel &model, const ListEditorOptions &options = {});

    // Moves items[from] so it ends up at index `to`, shifting the elements between. Out-of-range is a no-op.
    template <typename T>
    void MoveListElement(std::vector<T> &items, std::size_t from, std::size_t to)
    {
        if (from >= items.size() || to >= items.size() || from == to)
            return;
        if (from < to)
            std::rotate(items.begin() + from, items.begin() + from + 1, items.begin() + to + 1);
        else
            std::rotate(items.begin() + to, items.begin() + from, items.begin() + from + 1);
    }

    // RenderListEditor over a std::vector; new elements are copies of `newItem`.
    template <typename T>
    bool RenderVectorEditor(const char *label, std::vector<T> &items,
                            const std::function<bool(std::size_t index, T &item)> &renderElement,
                            const ListEditorOptions &options = {}, const T &newItem = T{})
    {
        return RenderListEditor(label, ListEditorModel{
            .count = items.size(),
            .renderElement = [&](std::size_t index) { return renderElement(index, items[index]); },
            .add = [&] { items.push_back(newItem); },
            .remove = [&](std::size_t index) { items.erase(items.begin() + static_cast<std::ptrdiff_t>(index)); },
            .move = [&](std::size_t from, std::size_t to) { MoveListElement(items, from, to); },
        }, options);
    }
}
