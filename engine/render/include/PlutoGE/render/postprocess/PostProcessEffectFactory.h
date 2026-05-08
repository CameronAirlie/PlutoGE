#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::render
{
    class IPostProcessEffect;

    std::unique_ptr<IPostProcessEffect> CreatePostProcessEffect(std::string_view typeName);
    const std::vector<std::string> &GetRegisteredPostProcessEffectTypes();
}