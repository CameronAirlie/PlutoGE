#pragma once

#include "PlutoGE/render/PostProcessGraph.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <unordered_map>

namespace PlutoGE::render
{
    class PostProcessResourcePool
    {
    public:
        explicit PostProcessResourcePool(rhi::IRenderDevice &device) : m_device(device) {}
        PostProcessResourcePool(const PostProcessResourcePool &) = delete;
        PostProcessResourcePool &operator=(const PostProcessResourcePool &) = delete;

        void Prepare(const PostProcessGraph &graph, const CompiledPostProcessGraph &compiled,
                     std::uint32_t width, std::uint32_t height);
        void Import(PostProcessResourceId id, rhi::TextureHandle texture);
        [[nodiscard]] rhi::TextureHandle Get(PostProcessResourceId id) const;
        void InvalidateHistory() noexcept;
        void Reset() noexcept;
        [[nodiscard]] std::size_t GetTransientAllocationCount() const noexcept { return m_transients.size(); }
        [[nodiscard]] std::size_t GetHistoryAllocationCount() const noexcept { return m_history.size(); }

    private:
        struct Key
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            rhi::Format format = rhi::Format::Undefined;
            auto operator<=>(const Key &) const = default;
        };
        struct Allocation
        {
            Key key;
            std::size_t lastPass = 0;
            rhi::Texture texture;
        };
        struct HistoryAllocation { Key key; rhi::Texture texture; };

        [[nodiscard]] static Key Resolve(const PostProcessResourceDescriptor &descriptor,
                                         std::uint32_t width, std::uint32_t height);
        [[nodiscard]] rhi::Texture CreateTexture(const Key &key, const std::string &name);

        rhi::IRenderDevice &m_device;
        std::vector<Allocation> m_transients;
        std::unordered_map<std::string, HistoryAllocation> m_history;
        std::vector<rhi::TextureHandle> m_bindings;
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
    };
}
