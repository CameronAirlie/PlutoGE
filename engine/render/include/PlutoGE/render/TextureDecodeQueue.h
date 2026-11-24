#pragma once

#include "PlutoGE/render/ImageDecoding.h"

#include <cstddef>
#include <future>
#include <optional>
#include <string>
#include <unordered_map>

namespace PlutoGE::render
{
    // Image decodes started ahead of use. A loader that knows which files it will need (a material's textures) starts
    // them here, so they decode on worker threads, side by side, while it carries on; the owner then takes each result
    // when it loads that file, waiting only for what has not finished. Owner-thread only: the decodes themselves run
    // on workers and touch nothing but their own copies.
    class TextureDecodeQueue
    {
    public:
        using Result = std::optional<DecodedImage>;
        // At most this many decodes wait to be taken: one that is never taken holds its pixels until Clear.
        static constexpr std::size_t kMaxPending = 64;

        TextureDecodeQueue() = default;
        TextureDecodeQueue(const TextureDecodeQueue &) = delete;
        TextureDecodeQueue &operator=(const TextureDecodeQueue &) = delete;
        // Joins running decodes (their results are dropped).
        ~TextureDecodeQueue() { Clear(); }

        // Starts decoding `path` unless it is already started or the queue is full (the file then decodes when taken).
        void Start(const std::string &path);
        [[nodiscard]] bool IsStarted(const std::string &path) const { return m_pending.contains(path); }
        // The started decode of `path` (waiting for it, with loading-screen checkpoints while a load runs), or
        // nothing when none was started. Taking removes it.
        [[nodiscard]] std::optional<Result> Take(const std::string &path);
        [[nodiscard]] std::size_t Pending() const noexcept { return m_pending.size(); }
        void Clear();

    private:
        std::unordered_map<std::string, std::future<Result>> m_pending;
    };
}
