#include "PlutoGE/render/PostProcessGraphExecutor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace PlutoGE::render
{
    void PostProcessGraphExecutor::Register(std::string name, Implementation implementation)
    {
        if (name.empty() || !implementation)
            throw std::invalid_argument("Post-process implementations require a name and callback");
        if (!m_implementations.emplace(std::move(name), std::move(implementation)).second)
            throw std::invalid_argument("Post-process implementation is already registered");
    }

    bool PostProcessGraphExecutor::HasImplementation(std::string_view name) const
    {
        return m_implementations.find(std::string(name)) != m_implementations.end();
    }

    void PostProcessGraphExecutor::Execute(const PostProcessGraph &graph,
                                           const CompiledPostProcessGraph &compiled,
                                           const PostProcessResourcePool &resources,
                                           std::uint32_t viewportWidth, std::uint32_t viewportHeight) const
    {
        if (viewportWidth == 0 || viewportHeight == 0)
            throw std::invalid_argument("Post-process execution extent must be non-zero");
        if (compiled.passOrder.size() != graph.GetPasses().size())
            throw std::invalid_argument("Compiled post-process graph does not match its passes");
        for (const auto passIndex : compiled.passOrder)
        {
            const auto &pass = graph.GetPass(passIndex);
            const auto implementation = m_implementations.find(pass.implementation);
            if (implementation == m_implementations.end())
                throw std::logic_error("Post-process pass implementation is not registered: " + pass.implementation);
            std::vector<ResolvedPostProcessInput> inputs;
            inputs.reserve(pass.inputs.size());
            for (const auto &input : pass.inputs)
            {
                const auto texture = resources.Get(input.resource);
                if (!texture) throw std::logic_error("Post-process input is not physically bound: " + pass.name);
                inputs.push_back({input.semantic, PostProcessInputSlot(input.semantic), texture});
            }
            std::vector<rhi::TextureHandle> outputs;
            outputs.reserve(pass.writes.size());
            for (const auto output : pass.writes)
            {
                const auto texture = resources.Get(output);
                if (!texture) throw std::logic_error("Post-process output is not physically bound: " + pass.name);
                if (std::ranges::any_of(inputs, [&](const auto &input) { return input.texture == texture; }))
                    throw std::logic_error("Post-process pass aliases a sampled input and render output");
                outputs.push_back(texture);
            }
            const auto &outputDescriptor = graph.GetResource(pass.writes.front());
            const auto width = (std::max)(1u, static_cast<std::uint32_t>(std::ceil(viewportWidth * outputDescriptor.widthScale)));
            const auto height = (std::max)(1u, static_cast<std::uint32_t>(std::ceil(viewportHeight * outputDescriptor.heightScale)));
            implementation->second({pass.name, inputs, outputs, width, height});
        }
    }
}
