#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace PlutoGE::ui
{
    // Undo and redo for an editor that keeps whole copies of a small document. Changes recorded while a control
    // is held (a drag, typing a name) become one step until EndFrame sees the control released.
    template <typename TDocument>
    class DocumentHistory
    {
    public:
        explicit DocumentHistory(std::size_t limit = 200) : m_limit(limit) {}

        // Call before applying a change, with the document as it was.
        void Record(TDocument before)
        {
            if (m_groupOpen)
                return;
            m_undo.push_back(std::move(before));
            if (m_undo.size() > m_limit)
                m_undo.erase(m_undo.begin());
            m_redo.clear();
            m_groupOpen = true;
        }

        void EndFrame(bool controlHeld)
        {
            if (!controlHeld)
                m_groupOpen = false;
        }
        // The next change starts a new step even if a control is still held.
        void CloseGroup() { m_groupOpen = false; }

        [[nodiscard]] bool CanUndo() const { return !m_undo.empty(); }
        [[nodiscard]] bool CanRedo() const { return !m_redo.empty(); }

        // Swap `current` with the neighbouring step. False when there is none.
        bool Undo(TDocument &current) { return Step(m_undo, m_redo, current); }
        bool Redo(TDocument &current) { return Step(m_redo, m_undo, current); }

    private:
        bool Step(std::vector<TDocument> &from, std::vector<TDocument> &to, TDocument &current)
        {
            if (from.empty())
                return false;
            to.push_back(std::move(current));
            current = std::move(from.back());
            from.pop_back();
            m_groupOpen = false;
            return true;
        }

        std::size_t m_limit;
        std::vector<TDocument> m_undo;
        std::vector<TDocument> m_redo;
        bool m_groupOpen = false;
    };
}
