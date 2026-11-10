#include "TextureCompressionQueue.h"

#include <algorithm>

namespace PlutoGE::render
{
    TextureCompressionQueue::TextureCompressionQueue(std::shared_ptr<const CompressedTextureStore> store,
                                                     std::uint32_t workerCount)
        : m_store(std::move(store))
    {
        for (std::uint32_t index = 0; index < std::max(workerCount, 1u); ++index)
            m_workers.emplace_back([this](std::stop_token stop) { Work(stop); });
    }

    TextureCompressionQueue::~TextureCompressionQueue()
    {
        for (auto &worker : m_workers)
            worker.request_stop();
        m_wake.notify_all();
        m_workers.clear();
    }

    void TextureCompressionQueue::Submit(Job job)
    {
        {
            std::lock_guard lock(m_mutex);
            m_jobs.push_back(std::move(job));
            ++m_outstanding;
        }
        m_wake.notify_one();
    }

    std::vector<TextureCompressionQueue::Result> TextureCompressionQueue::TakeCompleted()
    {
        std::lock_guard lock(m_mutex);
        m_outstanding -= m_completed.size();
        return std::exchange(m_completed, {});
    }

    std::size_t TextureCompressionQueue::Outstanding() const
    {
        std::lock_guard lock(m_mutex);
        return m_outstanding;
    }

    TextureCompressionQueue::Result TextureCompressionQueue::Run(const Job &job, const CompressedTextureStore *store)
    {
        Result result{job.ticket};
        const auto key = CompressedTextureStore::Key(job.rgba8, job.width, job.height, job.semantic, job.format);
        if (store)
            if (auto cached = store->Load(key); cached && cached->format == job.format && cached->width == job.width &&
                                                cached->height == job.height)
            {
                result.texture = std::move(cached);
                result.fromCache = true;
                return result;
            }
        result.texture = EncodeTexture(job.rgba8, job.width, job.height, job.semantic, job.format);
        if (store && result.texture)
            store->Save(key, *result.texture);
        return result;
    }

    void TextureCompressionQueue::Work(std::stop_token stop)
    {
        while (true)
        {
            Job job;
            {
                std::unique_lock lock(m_mutex);
                if (!m_wake.wait(lock, stop, [&] { return !m_jobs.empty(); }))
                    return;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            Result result{job.ticket};
            try
            {
                result = Run(job, m_store.get());
            }
            catch (...)
            {
                // A failed encode leaves the texture uncompressed; it must not end the worker.
            }
            std::lock_guard lock(m_mutex);
            m_completed.push_back(std::move(result));
        }
    }
}
