#include "PlutoGE/render/TextureDecodeQueue.h"

#include "PlutoGE/platform/LoadingWork.h"

#include <chrono>

namespace PlutoGE::render
{
    void TextureDecodeQueue::Start(const std::string &path)
    {
        if (path.empty() || m_pending.contains(path) || m_pending.size() >= kMaxPending)
            return;
        m_pending.emplace(path, std::async(std::launch::async, [path] { return DecodeImageFile(path); }));
    }

    std::optional<TextureDecodeQueue::Result> TextureDecodeQueue::Take(const std::string &path)
    {
        const auto found = m_pending.find(path);
        if (found == m_pending.end())
            return std::nullopt;
        auto decode = std::move(found->second);
        m_pending.erase(found);
        // A loading screen keeps presenting while the owner waits (as LoadingWork::Prepare does).
        while (decode.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            platform::LoadingWork::Checkpoint();
        return decode.get();
    }

    void TextureDecodeQueue::Clear()
    {
        for (auto &[path, decode] : m_pending)
            if (decode.valid())
                decode.wait();
        m_pending.clear();
    }
}
