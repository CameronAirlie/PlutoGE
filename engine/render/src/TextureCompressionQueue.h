#pragma once

#include "CompressedTextureStore.h"
#include "PlutoGE/render/TextureCompression.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace PlutoGE::render
{
    // Encodes textures on background threads so first use never waits for block compression. Callers submit RGBA8
    // texels with a ticket and collect finished chains from their own thread; the queue knows nothing about who
    // asked, so the renderer stays responsible for checking that a result is still wanted.
    class TextureCompressionQueue
    {
    public:
        struct Job
        {
            std::uint64_t ticket = 0;
            std::vector<std::byte> rgba8;
            std::uint32_t width = 0, height = 0;
            TextureSemantic semantic = TextureSemantic::Color;
            rhi::Format format = rhi::Format::Undefined;
        };
        struct Result
        {
            std::uint64_t ticket = 0;
            // nullopt when encoding failed; the texture then stays uncompressed.
            std::optional<EncodedTexture> texture;
            bool fromCache = false;
        };

        TextureCompressionQueue(std::shared_ptr<const CompressedTextureStore> store, std::uint32_t workerCount);
        ~TextureCompressionQueue();
        TextureCompressionQueue(const TextureCompressionQueue &) = delete;
        TextureCompressionQueue &operator=(const TextureCompressionQueue &) = delete;

        void Submit(Job job);
        [[nodiscard]] std::vector<Result> TakeCompleted();
        // Jobs submitted and not yet taken.
        [[nodiscard]] std::size_t Outstanding() const;

        // Loads `job` from the store, or encodes and stores it, on the calling thread.
        [[nodiscard]] static Result Run(const Job &job, const CompressedTextureStore *store);

    private:
        void Work(std::stop_token stop);

        std::shared_ptr<const CompressedTextureStore> m_store;
        mutable std::mutex m_mutex;
        std::condition_variable_any m_wake;
        std::deque<Job> m_jobs;
        std::vector<Result> m_completed;
        std::size_t m_outstanding = 0;
        std::vector<std::jthread> m_workers;
    };
}
