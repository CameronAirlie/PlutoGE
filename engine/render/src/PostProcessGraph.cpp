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
        if (descriptor.implementation.empty()) descriptor.implementation = descriptor.name;
        if (descriptor.writes.empty())
            throw std::invalid_argument("Post-process passes must declare an output");
        const auto validate = [this](PostProcessResourceId id)
        {
            if (!id || id.value >= m_resources.size())
                throw std::out_of_range("Post-process pass references an invalid resource");
        };
        for (const auto &input : descriptor.inputs) validate(input.resource);
        for (const auto id : descriptor.writes) validate(id);
        const auto &primaryOutput = m_resources[descriptor.writes.front().value];
        for (const auto output : descriptor.writes)
            if (m_resources[output.value].widthScale != primaryOutput.widthScale ||
                m_resources[output.value].heightScale != primaryOutput.heightScale)
                throw std::invalid_argument("Post-process pass outputs must have matching dimensions");
        for (std::size_t index = 0; index < descriptor.inputs.size(); ++index)
        {
            const auto &input = descriptor.inputs[index];
            if (std::find(descriptor.writes.begin(), descriptor.writes.end(), input.resource) != descriptor.writes.end())
                throw std::invalid_argument("Post-process passes cannot read and write the same immutable resource");
            for (std::size_t previous = 0; previous < index; ++previous)
                if (descriptor.inputs[previous].semantic == input.semantic)
                    throw std::invalid_argument("Post-process input semantics must be unique within a pass");
        }
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
            for (const auto &inputBinding : m_passes[passIndex].inputs)
            {
                const auto input = inputBinding.resource;
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
        result.resourceLifetimes.resize(m_resources.size());
        for (std::size_t scheduledIndex = 0; scheduledIndex < result.passOrder.size(); ++scheduledIndex)
        {
            const auto &pass = m_passes[result.passOrder[scheduledIndex]];
            const auto recordUse = [&](PostProcessResourceId id)
            {
                auto &lifetime = result.resourceLifetimes[id.value];
                if (!lifetime.IsUsed()) lifetime.firstPass = scheduledIndex;
                lifetime.lastPass = scheduledIndex;
            };
            for (const auto &input : pass.inputs) recordUse(input.resource);
            for (const auto output : pass.writes) recordUse(output);
        }
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
