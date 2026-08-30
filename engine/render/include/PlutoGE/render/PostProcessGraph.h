#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    struct PostProcessResourceId
    {
        static constexpr std::uint32_t Invalid = ~std::uint32_t{0};
        std::uint32_t value = Invalid;
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != Invalid; }
        auto operator<=>(const PostProcessResourceId &) const = default;
    };

    enum class PostProcessResourceLifetime : std::uint8_t
    {
        External,
        Transient,
        History,
    };

    struct PostProcessResourceDescriptor
    {
        std::string name;
        rhi::Format format = rhi::Format::R8G8B8A8Srgb;
        float widthScale = 1.0f;
        float heightScale = 1.0f;
        PostProcessResourceLifetime lifetime = PostProcessResourceLifetime::Transient;
    };

    struct PostProcessPassDescriptor
    {
        std::string name;
        std::vector<PostProcessResourceId> reads;
        std::vector<PostProcessResourceId> writes;
    };

    struct CompiledPostProcessGraph
    {
        std::vector<std::size_t> passOrder;
    };

    class PostProcessGraph
    {
    public:
        PostProcessResourceId AddResource(PostProcessResourceDescriptor descriptor);
        std::size_t AddPass(PostProcessPassDescriptor descriptor);
        [[nodiscard]] CompiledPostProcessGraph Compile() const;
        [[nodiscard]] const PostProcessResourceDescriptor &GetResource(PostProcessResourceId id) const;
        [[nodiscard]] const PostProcessPassDescriptor &GetPass(std::size_t index) const;
        [[nodiscard]] std::span<const PostProcessResourceDescriptor> GetResources() const noexcept { return m_resources; }
        [[nodiscard]] std::span<const PostProcessPassDescriptor> GetPasses() const noexcept { return m_passes; }
        void Clear() noexcept;

    private:
        std::vector<PostProcessResourceDescriptor> m_resources;
        std::vector<PostProcessPassDescriptor> m_passes;
    };
}
