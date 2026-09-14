#include "PlutoGE/scene/SceneStreaming.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/platform/ContentPack.h"
#include <atomic>
#include <fstream>
#include <map>
#include <thread>
#include <limits>
#include <stdexcept>

namespace PlutoGE::scene
{
    struct SceneStreaming::Impl
    {
        struct Request
        {
            SceneSectionState state = SceneSectionState::Reading;
            bool activate = true;
            bool forget = false;
            std::atomic<bool> done{false};
            std::atomic<std::uint64_t> bytes{0}, size{0};
            std::string data, error;
            std::jthread worker; // Destroy/join before the data it references.
        };
        Scene &scene;
        inline static std::atomic<std::uint64_t> nextGeneration{1};
        std::uint64_t generation = nextGeneration.fetch_add(1);
        SceneSectionID next = 1;
        std::map<SceneSectionID, std::unique_ptr<Request>> requests;
        explicit Impl(Scene &value) : scene(value) {}
    };
    SceneStreaming::SceneStreaming(Scene &scene) : m_impl(std::make_unique<Impl>(scene)) {}
    SceneStreaming::~SceneStreaming() = default;
    std::uint64_t SceneStreaming::Generation() const { return m_impl->generation; }
    SceneSectionID SceneStreaming::Load(const std::filesystem::path &path, bool activateWhenReady)
    {
        if (m_impl->requests.size() >= 64 || m_impl->next == std::numeric_limits<SceneSectionID>::max()) return 0;
        std::size_t pending = 0;
        for (const auto &[id, request] : m_impl->requests)
            if (request->state == SceneSectionState::Reading || request->state == SceneSectionState::Ready || !request->done.load(std::memory_order_acquire)) ++pending;
        if (pending >= 4) return 0;
        auto request = std::make_unique<Impl::Request>();
        request->activate = activateWhenReady;
        auto *raw = request.get();
        raw->worker = std::jthread([raw, path](std::stop_token stop)
        {
            try
            {
                std::error_code fileError;
                if (!content::IsRegularFile(path, fileError) || fileError) throw std::runtime_error("Scene section is not a regular file");
                const auto size = content::FileSize(path, fileError);
                if (fileError || size == 0 || size > 64 * 1024 * 1024) throw std::runtime_error("Scene section size must be between 1 byte and 64 MiB");
                if (stop.stop_requested()) { raw->done.store(true, std::memory_order_release); return; }
                content::InputFile input(path, std::ios::binary);
                if (!input.is_open()) throw std::runtime_error("Cannot open scene section");
                raw->size = static_cast<std::uint64_t>(size);
                raw->data.resize(static_cast<std::size_t>(size));
                input.seekg(0);
                std::size_t offset = 0;
                while (offset < raw->data.size() && !stop.stop_requested())
                {
                    const auto count = std::min<std::size_t>(65536, raw->data.size() - offset);
                    if (!input.read(raw->data.data() + offset, count)) throw std::runtime_error("Scene section read was interrupted or truncated");
                    offset += count;
                    raw->bytes = offset;
                }
                if (stop.stop_requested()) std::string{}.swap(raw->data);
            }
            catch (const std::exception &error) { raw->error = error.what(); std::string{}.swap(raw->data); }
            raw->done.store(true, std::memory_order_release);
        });
        const auto id = m_impl->next++;
        m_impl->requests.emplace(id, std::move(request));
        return id;
    }
    SceneSectionStatus SceneStreaming::Status(SceneSectionID id) const
    {
        const auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->second->forget) return {SceneSectionState::Failed, 0, "Unknown scene section"};
        const auto &r = *it->second;
        const auto size = r.size.load();
        const float progress = r.state == SceneSectionState::Active ? 1.0f : size ? 0.9f * float(r.bytes.load()) / float(size) : 0;
        // Worker error/data may only be read after its release publication.
        return {r.state, progress, r.done.load(std::memory_order_acquire) ? r.error : std::string{}};
    }
    bool SceneStreaming::Activate(SceneSectionID id)
    {
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end()) return false;
        auto &r = *it->second;
        if (r.state != SceneSectionState::Reading && r.state != SceneSectionState::Ready) return false;
        r.activate = true;
        return true;
    }
    bool SceneStreaming::Cancel(SceneSectionID id)
    {
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end()) return false;
        auto &r = *it->second;
        if (r.state != SceneSectionState::Reading && r.state != SceneSectionState::Ready) return false;
        r.worker.request_stop();
        r.state = SceneSectionState::Cancelled;
        return true;
    }
    bool SceneStreaming::Unload(SceneSectionID id)
    {
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end() || it->second->state != SceneSectionState::Active) return false;
        m_impl->scene.UnloadSectionEntities(id);
        it->second->state = SceneSectionState::Unloaded;
        return true;
    }
    bool SceneStreaming::Forget(SceneSectionID id)
    {
        auto it = m_impl->requests.find(id);
        if (it == m_impl->requests.end()) return false;
        const auto state = it->second->state;
        if (state == SceneSectionState::Reading || state == SceneSectionState::Ready || state == SceneSectionState::Active) return false;
        if (!it->second->done.load(std::memory_order_acquire))
        {
            it->second->forget = true; // Reclaim on Pump after the worker releases its storage.
            return true;
        }
        m_impl->requests.erase(it);
        return true;
    }
    void SceneStreaming::Pump()
    {
        bool activated = false;
        for (auto &[id, ptr] : m_impl->requests)
        {
            auto &r = *ptr;
            if (!r.done.load(std::memory_order_acquire)) continue;
            if (r.state == SceneSectionState::Cancelled) { std::string{}.swap(r.data); continue; }
            if (r.state == SceneSectionState::Reading) r.state = r.error.empty() ? SceneSectionState::Ready : SceneSectionState::Failed;
            if (r.state != SceneSectionState::Ready || !r.activate || activated) continue;
            activated = true;
            try
            {
                if (!r.data.starts_with("SCENE\t1\n") && !r.data.starts_with("SCENE\t1\r\n"))
                    throw std::runtime_error("Unsupported or missing scene section header");
                bool unsupported = false;
                auto section = SceneSerializer::LoadFromString(r.data, &r.error, [&](std::string_view message)
                {
                    unsupported |= message.find("skipped unknown") != std::string_view::npos;
                });
                if (unsupported) throw std::runtime_error("Scene section contains an unsupported component");
                if (!section || !r.error.empty()) throw std::runtime_error(r.error.empty() ? "Invalid scene section" : r.error);
                m_impl->scene.AdoptSectionEntities(*section, id);
                r.state = SceneSectionState::Active;
            }
            catch (const std::exception &error) { r.error = error.what(); r.state = SceneSectionState::Failed; }
            std::string{}.swap(r.data);
        }
        std::erase_if(m_impl->requests, [](const auto &entry)
        {
            return entry.second->forget && entry.second->done.load(std::memory_order_acquire);
        });
    }
    void SceneStreaming::Reset()
    {
        for (auto &[id, request] : m_impl->requests)
        {
            request->worker.request_stop();
            if (request->state == SceneSectionState::Active) m_impl->scene.UnloadSectionEntities(id);
        }
        m_impl->requests.clear();
        m_impl->generation = Impl::nextGeneration.fetch_add(1);
    }
}
