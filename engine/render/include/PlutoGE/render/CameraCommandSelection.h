#pragma once
#include "PlutoGE/render/RenderCommandView.h"
#include <algorithm>
#include <vector>

namespace PlutoGE::render
{
    // The commands a camera draws: all of them but those that only cast shadows (RenderCommand::shadowOnly). Returns
    // `commands` itself when none is shadow-only, so ordinary frames pay one scan and no copy. Keep one instance per
    // renderer: the returned view borrows its storage until the next Select.
    class CameraCommandSelection
    {
    public:
        [[nodiscard]] RenderCommandView Select(RenderCommandView commands)
        {
            if (std::ranges::none_of(commands, [](const RenderCommand &command) { return command.shadowOnly; }))
                return commands;
            m_selected.clear();
            m_selected.reserve(commands.size());
            for (const auto &command : commands)
                if (!command.shadowOnly)
                    m_selected.push_back(&command);
            return RenderCommandView(m_selected);
        }

    private:
        std::vector<const RenderCommand *> m_selected;
    };
}
