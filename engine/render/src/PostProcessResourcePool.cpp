#include "PlutoGE/render/PostProcessResourcePool.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>

namespace PlutoGE::render
{
    PostProcessResourcePool::Key PostProcessResourcePool::Resolve(
        const PostProcessResourceDescriptor &descriptor, std::uint32_t width, std::uint32_t height)
    {
        return {
            (std::max)(1u, static_cast<std::uint32_t>(std::ceil(width * descriptor.widthScale))),
            (std::max)(1u, static_cast<std::uint32_t>(std::ceil(height * descriptor.heightScale))),
            descriptor.format};
    }

    rhi::Texture PostProcessResourcePool::CreateTexture(const Key &key, const std::string &name)
    {
        return rhi::Texture(m_device, m_device.CreateTexture(
            {.width = key.width, .height = key.height, .format = key.format,
             .usage = rhi::TextureUsage::ColorAttachment, .debugName = name, .sampled = true}));
    }

    void PostProcessResourcePool::Prepare(const PostProcessGraph &graph,
                                          const CompiledPostProcessGraph &compiled,
                                          std::uint32_t width, std::uint32_t height)
    {
        if (width == 0 || height == 0) throw std::invalid_argument("Post-process extent must be non-zero");
        if (compiled.resourceLifetimes.size() != graph.GetResources().size())
            throw std::invalid_argument("Compiled post-process graph does not match its resource declarations");
        if (m_width != width || m_height != height)
        {
            m_transients.clear();
            m_history.clear();
            m_width = width;
            m_height = height;
        }
        else
            for (auto &allocation : m_transients)
                allocation.lastPass = CompiledPostProcessGraph::ResourceLifetime::Unused;
        m_bindings.assign(graph.GetResources().size(), {});

        std::vector<std::size_t> transientResources;
        for (std::size_t index = 0; index < graph.GetResources().size(); ++index)
        {
            const auto &descriptor = graph.GetResources()[index];
            const auto &lifetime = compiled.resourceLifetimes[index];
            if (!lifetime.IsUsed() || descriptor.lifetime == PostProcessResourceLifetime::External) continue;
            const auto key = Resolve(descriptor, width, height);
            if (descriptor.lifetime == PostProcessResourceLifetime::History)
            {
                auto found = m_history.find(descriptor.name);
                if (found != m_history.end() && found->second.key != key) m_history.erase(found);
                found = m_history.find(descriptor.name);
                if (found == m_history.end())
                    found = m_history.emplace(descriptor.name, HistoryAllocation{key, CreateTexture(key, descriptor.name)}).first;
                m_bindings[index] = found->second.texture.Get();
                continue;
            }

            transientResources.push_back(index);
        }
        std::ranges::sort(transientResources, [&](std::size_t lhs, std::size_t rhs)
        {
            return compiled.resourceLifetimes[lhs].firstPass < compiled.resourceLifetimes[rhs].firstPass;
        });
        for (const auto index : transientResources)
        {
            const auto &descriptor = graph.GetResources()[index];
            const auto &lifetime = compiled.resourceLifetimes[index];
            const auto key = Resolve(descriptor, width, height);
            auto reusable = std::find_if(m_transients.begin(), m_transients.end(), [&](const Allocation &allocation)
            {
                return allocation.key == key &&
                       (allocation.lastPass == CompiledPostProcessGraph::ResourceLifetime::Unused ||
                        allocation.lastPass < lifetime.firstPass);
            });
            if (reusable == m_transients.end())
            {
                m_transients.push_back({key, lifetime.lastPass, CreateTexture(key, descriptor.name)});
                reusable = std::prev(m_transients.end());
            }
            else
                reusable->lastPass = lifetime.lastPass;
            m_bindings[index] = reusable->texture.Get();
        }
    }

    void PostProcessResourcePool::Import(PostProcessResourceId id, rhi::TextureHandle texture)
    {
        if (!id || id.value >= m_bindings.size()) throw std::out_of_range("Invalid imported post-process resource");
        if (!texture) throw std::invalid_argument("Imported post-process texture is invalid");
        m_bindings[id.value] = texture;
    }

    rhi::TextureHandle PostProcessResourcePool::Get(PostProcessResourceId id) const
    {
        if (!id || id.value >= m_bindings.size()) throw std::out_of_range("Invalid post-process resource binding");
        return m_bindings[id.value];
    }

    void PostProcessResourcePool::InvalidateHistory() noexcept { m_history.clear(); }
    void PostProcessResourcePool::Reset() noexcept
    {
        m_bindings.clear();
        m_transients.clear();
        m_history.clear();
        m_width = m_height = 0;
    }
}
