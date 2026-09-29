#pragma once
#include "PlutoGE/render/RenderCommand.h"
#include <span>
#include <memory>

namespace PlutoGE::render
{
    // Single-writer frontend scene. Producers publish only when their revision
    // changes; one frame lease per producer handles disabling, removal and scene
    // switches without callbacks into a destroyed renderer. Mutable arrays use
    // the transient submission path until they acquire authoritative revisions.
    class RetainedRenderScene
    {
    public:
        struct Handle
        {
            std::uint32_t index = ~0u;
            std::uint64_t generation = 0;
            bool operator==(const Handle &) const = default;
        };
        struct Revisions { std::uint64_t transform = 0, geometry = 0, material = 0; };
        struct Stats
        {
            std::size_t activeProducers = 0, activeCommands = 0;
            std::size_t updates = 0, reusedProducers = 0, removedProducers = 0;
            std::size_t rebuiltCommands = 0;
        };
        RetainedRenderScene();
        ~RetainedRenderScene();
        RetainedRenderScene(const RetainedRenderScene &) = delete;
        RetainedRenderScene &operator=(const RetainedRenderScene &) = delete;
        void BeginFrame();
        // A scene lease replaces per-object leases. False means unchanged
        // producers remain registered; true requires initial publication.
        bool BeginScope(const std::shared_ptr<const void> &lifetime);
        void EndScope();
        void Remove(std::uint64_t producer);
        Handle Publish(std::uint64_t producer, std::uint64_t revision, std::span<const RenderCommand> commands);
        // Completes queued changes before exposing any borrowed command views.
        void Synchronize();
        [[nodiscard]] bool IsValid(Handle handle) const;
        [[nodiscard]] Revisions GetRevisions(Handle handle) const;
        [[nodiscard]] std::span<RenderCommand *const> Commands() const;
        // Sequence/sort-key revision; transform-only updates preserve the list.
        [[nodiscard]] std::uint64_t Revision() const;
        [[nodiscard]] const Stats &GetStats() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
