#include "VulkanPipelines.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace PlutoGE::render::rhi::vulkan
{
    VkPipeline VulkanPipelineCompiler::Job::Resolve(VkResult *result)
    {
        if (const auto pipeline = m_pipeline.load(std::memory_order_acquire))
        {
            if (result) *result = VK_SUCCESS;
            return pipeline;
        }
        RunIfQueued();
        std::unique_lock lock(m_mutex);
        m_done.wait(lock, [&] { return m_state == State::Done; });
        if (result) *result = m_result;
        return m_pipeline.load(std::memory_order_relaxed);
    }

    void VulkanPipelineCompiler::Job::RunIfQueued()
    {
        Compile compile;
        {
            std::lock_guard lock(m_mutex);
            if (m_state != State::Queued)
                return;
            m_state = State::Running;
            compile = std::move(m_compile);
        }
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkResult result = VK_ERROR_UNKNOWN;
        try
        {
            result = compile(pipeline);
        }
        catch (...)
        {
            // A compile never throws through a worker; the failure is reported to whoever resolves the pipeline.
            pipeline = VK_NULL_HANDLE;
        }
        compile = {}; // releases the create-info storage outside the lock
        {
            std::lock_guard lock(m_mutex);
            m_result = result;
            m_pipeline.store(result == VK_SUCCESS ? pipeline : VK_NULL_HANDLE, std::memory_order_release);
            m_state = State::Done;
            m_finished.store(true, std::memory_order_release);
        }
        m_done.notify_all();
    }

    VulkanPipelineCompiler::VulkanPipelineCompiler(unsigned workers)
    {
        m_workers.reserve(workers);
        for (unsigned i = 0; i < workers; ++i)
            m_workers.emplace_back([this] { Work(); });
    }

    VulkanPipelineCompiler::~VulkanPipelineCompiler()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_wake.notify_all();
        for (auto &worker : m_workers)
            worker.join();
        // Workers drain the queue before they stop; nothing is left, but never leave a compile unrun.
        for (auto &job : m_queue)
            job->RunIfQueued();
    }

    std::shared_ptr<VulkanPipelineCompiler::Job> VulkanPipelineCompiler::Submit(Compile compile)
    {
        // Counted while it runs, on whichever thread runs it, for the idle callback.
        auto job = std::make_shared<Job>([this, compile = std::move(compile)](VkPipeline &pipeline)
        {
            {
                std::lock_guard lock(m_mutex);
                ++m_compiling;
            }
            struct Finished
            {
                VulkanPipelineCompiler &compiler;
                ~Finished()
                {
                    {
                        std::lock_guard lock(compiler.m_mutex);
                        --compiler.m_compiling;
                        ++compiler.m_compiledSinceIdle;
                    }
                    compiler.NotifyIfIdle();
                }
            } finished{*this};
            return compile(pipeline);
        });
        if (m_workers.empty())
        {
            job->RunIfQueued();
            return job;
        }
        {
            std::lock_guard lock(m_mutex);
            m_queue.push_back(job);
        }
        m_wake.notify_one();
        return job;
    }

    void VulkanPipelineCompiler::SetIdleCallback(std::function<void()> callback)
    {
        std::lock_guard lock(m_mutex);
        m_idle = std::move(callback);
    }

    void VulkanPipelineCompiler::NotifyIfIdle()
    {
        std::function<void()> idle;
        {
            std::lock_guard lock(m_mutex);
            // Queued jobs a resolver already ran stay queued until a worker pops them; they count as finished.
            const bool queued = std::any_of(m_queue.begin(), m_queue.end(), [](const auto &job) { return !job->IsDone(); });
            // A serial compiler is idle after every compile: it persists only when the device closes.
            if (m_workers.empty() || m_compiling || queued || !m_compiledSinceIdle || !m_idle)
                return;
            m_compiledSinceIdle = 0;
            idle = m_idle;
        }
        try { idle(); }
        catch (...) {} // persisting is best effort; it never fails a compile
    }

    unsigned VulkanPipelineCompiler::DefaultWorkerCount() noexcept
    {
        if (const char *setting = std::getenv("PLUTO_PIPELINE_COMPILE_THREADS"); setting && *setting)
        {
            char *end = nullptr;
            const auto count = std::strtoul(setting, &end, 10);
            if (end && *end == 0)
                return static_cast<unsigned>(std::min<unsigned long>(count, 64));
        }
        return std::clamp(std::thread::hardware_concurrency() / 2u, 2u, 8u);
    }

    void VulkanPipelineCompiler::Work()
    {
        for (;;)
        {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(m_mutex);
                m_wake.wait(lock, [&] { return m_stopping || !m_queue.empty(); });
                if (m_queue.empty())
                    return;
                job = std::move(m_queue.front());
                m_queue.pop_front();
            }
            job->RunIfQueued();
            NotifyIfIdle();
        }
    }

    namespace
    {
        // The cache directory a user's applications keep regenerable data in.
        std::filesystem::path UserCacheDirectory()
        {
#ifdef _WIN32
            if (const char *local = std::getenv("LOCALAPPDATA"); local && *local)
                return std::filesystem::path(local);
#else
            if (const char *cache = std::getenv("XDG_CACHE_HOME"); cache && *cache)
                return std::filesystem::path(cache);
            if (const char *home = std::getenv("HOME"); home && *home)
                return std::filesystem::path(home) / ".cache";
#endif
            return {};
        }

        std::string ExecutableStem()
        {
#ifdef _WIN32
            std::wstring path(MAX_PATH, L'\0');
            for (;;)
            {
                const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
                if (length == 0) return "PlutoGE";
                if (length < path.size()) { path.resize(length); break; }
                path.resize(path.size() * 2);
            }
            return std::filesystem::path(path).stem().string();
#else
            std::error_code error;
            const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
            return error ? std::string("PlutoGE") : path.stem().string();
#endif
        }
    }

    std::filesystem::path VulkanPipelineCacheFile::DefaultPath()
    {
        if (const char *setting = std::getenv("PLUTO_PIPELINE_CACHE"); setting && std::string_view(setting) == "0")
            return {};
        const auto root = UserCacheDirectory();
        // Per executable: games built with the engine do not share (or evict) each other's pipelines.
        return root.empty() ? std::filesystem::path{} : root / "PlutoGE" / "PipelineCache" / (ExecutableStem() + ".vkpipelines");
    }

    VulkanPipelineCacheFile::VulkanPipelineCacheFile(VkDevice device, const VkPhysicalDeviceProperties &properties,
                                                     std::filesystem::path path)
        : m_device(device), m_path(std::move(path))
    {
        std::vector<char> data;
        if (!m_path.empty())
        {
            std::ifstream input(m_path, std::ios::binary);
            data.assign(std::istreambuf_iterator<char>(input), {});
        }
        // Drivers validate the header themselves, but some accept foreign data poorly: check it matches this device.
        VkPipelineCacheHeaderVersionOne header{};
        const bool complete = data.size() >= sizeof(header);
        if (complete)
            std::memcpy(&header, data.data(), sizeof(header));
        const bool matches = complete && header.headerSize >= sizeof(header) &&
                             header.headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
                             header.vendorID == properties.vendorID && header.deviceID == properties.deviceID &&
                             std::memcmp(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
        VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        if (matches)
        {
            info.initialDataSize = data.size();
            info.pInitialData = data.data();
        }
        if (vkCreatePipelineCache(m_device, &info, nullptr, &m_cache) != VK_SUCCESS && matches)
        {
            // Rejected data: start empty rather than without a cache.
            info.initialDataSize = 0;
            info.pInitialData = nullptr;
            if (vkCreatePipelineCache(m_device, &info, nullptr, &m_cache) != VK_SUCCESS)
                m_cache = VK_NULL_HANDLE;
        }
    }

    VulkanPipelineCacheFile::~VulkanPipelineCacheFile()
    {
        if (m_cache)
            vkDestroyPipelineCache(m_device, m_cache, nullptr);
    }

    bool VulkanPipelineCacheFile::Save(std::string *error) const
    {
        if (!m_cache || m_path.empty())
            return true;
        // One save at a time; the data is read while no compile adds to the cache, then written without blocking them.
        std::lock_guard saving(m_saving);
        std::vector<char> data;
        {
            std::unique_lock exclusive(m_access);
            std::size_t size = 0;
            if (vkGetPipelineCacheData(m_device, m_cache, &size, nullptr) != VK_SUCCESS || size == 0 || size == m_savedSize)
                return true;
            data.resize(size);
            if (vkGetPipelineCacheData(m_device, m_cache, &size, data.data()) != VK_SUCCESS)
                return true;
            data.resize(size);
        }
        std::error_code fileError;
        std::filesystem::create_directories(m_path.parent_path(), fileError);
        // Written aside and renamed over the old file, so another instance never reads half of it.
        const auto temporary = m_path.parent_path() / (m_path.filename().string() + "." +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!output) fileError = std::make_error_code(std::errc::io_error);
        }
        if (!fileError) std::filesystem::rename(temporary, m_path, fileError);
        if (!fileError)
        {
            m_savedSize = data.size();
            return true;
        }
        std::filesystem::remove(temporary, fileError);
        if (error) *error = "Could not save the pipeline cache to " + m_path.string();
        return false;
    }
}
