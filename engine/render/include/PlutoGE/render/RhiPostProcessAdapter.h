#pragma once

#include "PlutoGE/render/BasicRenderer.h"

#include <optional>
#include <string_view>

namespace PlutoGE::render
{
    class IPostProcessEffect;

    // Translation boundary between editor-facing effect objects and the
    // compact, backend-neutral packets consumed by the RHI renderer.
    [[nodiscard]] std::optional<BasicPostProcessEffect> AdaptPostProcessEffect(
        const IPostProcessEffect &effect);
    [[nodiscard]] bool IsRhiPostProcessEffectSupported(std::string_view typeName) noexcept;
}
