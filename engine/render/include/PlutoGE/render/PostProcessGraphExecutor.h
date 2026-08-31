#pragma once

#include "PlutoGE/render/PostProcessResourcePool.h"

#include <functional>
#include <unordered_map>

namespace PlutoGE::render
{
    struct ResolvedPostProcessInput
    {
        PostProcessPassDescriptor::InputSemantic semantic;
        std::uint32_t slot = 0;
        rhi::TextureHandle texture;
    };

    struct PostProcessPassContext
    {
        std::string_view name;
        std::span<const ResolvedPostProcessInput> inputs;
        std::span<const rhi::TextureHandle> outputs;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    class PostProcessGraphExecutor
    {
    public:
        using Implementation = std::function<void(const PostProcessPassContext &)>;

        void Register(std::string name, Implementation implementation);
        void Execute(const PostProcessGraph &graph, const CompiledPostProcessGraph &compiled,
                     const PostProcessResourcePool &resources,
                     std::uint32_t viewportWidth, std::uint32_t viewportHeight) const;
        [[nodiscard]] bool HasImplementation(std::string_view name) const;

    private:
        std::unordered_map<std::string, Implementation> m_implementations;
    };
}
