#include "PlutoGE/render/PostProcessGraph.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

namespace PlutoGE::render
{
    PostProcessResourceId PostProcessGraph::AddResource(PostProcessResourceDescriptor descriptor)
    {
        if (descriptor.name.empty())
            throw std::invalid_argument("Post-process resources require a name");
        if (descriptor.widthScale <= 0.0f || descriptor.heightScale <= 0.0f)
            throw std::invalid_argument("Post-process resource scales must be positive");
        if (descriptor.format == rhi::Format::Undefined)
            throw std::invalid_argument("Post-process resources require a defined format");
        const auto id = PostProcessResourceId{static_cast<std::uint32_t>(m_resources.size())};
        m_resources.push_back(std::move(descriptor));
        return id;
    }

    std::size_t PostProcessGraph::AddPass(PostProcessPassDescriptor descriptor)
    {
        if (descriptor.name.empty())
            throw std::invalid_argument("Post-process passes require a name");
        if (descriptor.writes.empty())
            throw std::invalid_argument("Post-process passes must declare an output");
        const auto validate = [this](PostProcessResourceId id)
        {
            if (!id || id.value >= m_resources.size())
                throw std::out_of_range("Post-process pass references an invalid resource");
        };
        for (const auto id : descriptor.reads) validate(id);
        for (const auto id : descriptor.writes) validate(id);
        for (const auto read : descriptor.reads)
            if (std::find(descriptor.writes.begin(), descriptor.writes.end(), read) != descriptor.writes.end())
                throw std::invalid_argument("Post-process passes cannot read and write the same immutable resource");
        m_passes.push_back(std::move(descriptor));
        return m_passes.size() - 1;
    }

    CompiledPostProcessGraph PostProcessGraph::Compile() const
    {
        std::vector<std::size_t> writer(m_resources.size(), static_cast<std::size_t>(-1));
        for (std::size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
            for (const auto output : m_passes[passIndex].writes)
            {
                if (writer[output.value] != static_cast<std::size_t>(-1))
                    throw std::logic_error("Post-process resources must have exactly one writer");
                if (m_resources[output.value].lifetime == PostProcessResourceLifetime::External)
                    throw std::logic_error("Post-process passes cannot write external resources");
                writer[output.value] = passIndex;
            }

        std::vector<std::vector<std::size_t>> edges(m_passes.size());
        std::vector<std::size_t> indegree(m_passes.size());
        for (std::size_t passIndex = 0; passIndex < m_passes.size(); ++passIndex)
            for (const auto input : m_passes[passIndex].reads)
            {
                const auto producer = writer[input.value];
                if (producer == static_cast<std::size_t>(-1))
                {
                    if (m_resources[input.value].lifetime == PostProcessResourceLifetime::Transient)
                        throw std::logic_error("Transient post-process input has no producer");
                    continue;
                }
                edges[producer].push_back(passIndex);
                ++indegree[passIndex];
            }

        std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
        for (std::size_t index = 0; index < indegree.size(); ++index)
            if (indegree[index] == 0) ready.push(index);
        CompiledPostProcessGraph result;
        while (!ready.empty())
        {
            const auto pass = ready.top();
            ready.pop();
            result.passOrder.push_back(pass);
            for (const auto dependent : edges[pass])
                if (--indegree[dependent] == 0) ready.push(dependent);
        }
        if (result.passOrder.size() != m_passes.size())
            throw std::logic_error("Post-process graph contains a dependency cycle");
        return result;
    }

    const PostProcessResourceDescriptor &PostProcessGraph::GetResource(PostProcessResourceId id) const
    {
        if (!id || id.value >= m_resources.size()) throw std::out_of_range("Invalid post-process resource");
        return m_resources[id.value];
    }

    const PostProcessPassDescriptor &PostProcessGraph::GetPass(std::size_t index) const
    {
        return m_passes.at(index);
    }

    void PostProcessGraph::Clear() noexcept
    {
        m_passes.clear();
        m_resources.clear();
    }
}
