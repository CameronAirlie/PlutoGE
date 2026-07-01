#pragma once

#include <glad/glad.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    struct IndirectDrawGroupingKey
    {
        const void *shader = nullptr;
        const void *material = nullptr;
        const void *mesh = nullptr;
        bool skinned = false;
        bool alphaTested = false;
    };

    [[nodiscard]] inline bool CanGroupGeometryIndirectDraws(const IndirectDrawGroupingKey &head,
                                                             const IndirectDrawGroupingKey &candidate)
    {
        return !head.skinned && !candidate.skinned &&
               head.shader == candidate.shader &&
               head.material == candidate.material &&
               head.mesh == candidate.mesh;
    }

    [[nodiscard]] inline bool CanGroupShadowIndirectDraws(const IndirectDrawGroupingKey &head,
                                                           const IndirectDrawGroupingKey &candidate)
    {
        return !head.skinned && !candidate.skinned &&
               head.mesh == candidate.mesh &&
               head.alphaTested == candidate.alphaTested &&
               (!head.alphaTested || head.material == candidate.material);
    }

    struct DrawElementsIndirectCommand
    {
        std::uint32_t count = 0;
        std::uint32_t instanceCount = 0;
        std::uint32_t firstIndex = 0;
        std::int32_t baseVertex = 0;
        std::uint32_t baseInstance = 0;
    };
    static_assert(sizeof(DrawElementsIndirectCommand) == 5 * sizeof(std::uint32_t));

    inline DrawElementsIndirectCommand BuildDrawElementsIndirectCommand(
        std::uint32_t indexCount,
        std::size_t instanceCount,
        std::uint32_t firstIndex,
        std::size_t baseInstance)
    {
        return DrawElementsIndirectCommand{
            .count = indexCount,
            .instanceCount = static_cast<std::uint32_t>(instanceCount),
            .firstIndex = firstIndex,
            .baseVertex = 0,
            .baseInstance = static_cast<std::uint32_t>(baseInstance),
        };
    }

    inline void UploadIndirectDrawCommands(
        unsigned int &buffer,
        std::size_t &capacity,
        const std::vector<DrawElementsIndirectCommand> &commands)
    {
        if (commands.empty())
        {
            return;
        }

        if (buffer == 0)
        {
            glGenBuffers(1, &buffer);
        }

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, buffer);
        if (capacity < commands.size())
        {
            capacity = std::max(commands.size(), capacity == 0 ? commands.size() : capacity * 2);
            glBufferData(GL_DRAW_INDIRECT_BUFFER,
                         static_cast<GLsizeiptr>(capacity * sizeof(DrawElementsIndirectCommand)),
                         nullptr,
                         GL_STREAM_DRAW);
        }
        glBufferSubData(GL_DRAW_INDIRECT_BUFFER,
                        0,
                        static_cast<GLsizeiptr>(commands.size() * sizeof(DrawElementsIndirectCommand)),
                        commands.data());
    }
}
