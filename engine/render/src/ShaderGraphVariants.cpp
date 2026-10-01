#include "PlutoGE/render/ShaderGraphVariants.h"
#include "PlutoGE/render/ShaderGraphCodegen.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::render
{
    std::string_view ShaderGraphVariantEntryPoint(ShaderGraphVariantStage stage) noexcept
    {
        switch (stage)
        {
        case ShaderGraphVariantStage::Surface: return "fragmentMain";
        case ShaderGraphVariantStage::Color: return "colorMain";
        case ShaderGraphVariantStage::ColorMotion: return "colorMotionMain";
        case ShaderGraphVariantStage::Coverage: return "coverageMain";
        }
        return "fragmentMain";
    }

    namespace
    {
        struct VariantKey
        {
            std::uint64_t structure = 0;
            ShaderGraphVariantStage stage{};
            bool operator==(const VariantKey &) const = default;
        };
        struct VariantKeyHash
        {
            std::size_t operator()(const VariantKey &key) const noexcept
            {
                return std::hash<std::uint64_t>{}(key.structure * 31u + static_cast<std::uint64_t>(key.stage));
            }
        };

        std::string Hex(std::uint64_t value)
        {
            std::ostringstream out;
            out << std::hex;
            out.width(16);
            out.fill('0');
            out << value;
            return out.str();
        }

        void Mix(std::uint64_t &hash, std::string_view bytes)
        {
            for (const char byte : bytes)
            {
                hash ^= static_cast<unsigned char>(byte);
                hash *= 1099511628211ull;
            }
            hash ^= 0xffu; // Separates consecutive fields.
            hash *= 1099511628211ull;
        }

        std::string ReadFile(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot read " + path.string());
            return {std::istreambuf_iterator<char>(file), {}};
        }

        // Publishes complete files only, so concurrent processes sharing the
        // cache never read a partially written variant.
        void WriteFileAtomically(const std::filesystem::path &path, std::string_view bytes)
        {
            auto temporary = path;
            temporary += ".tmp" + std::to_string(std::random_device{}()); // Unique across processes.
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!file) throw std::runtime_error("Cannot write " + temporary.string());
            }
            std::error_code error;
            std::filesystem::rename(temporary, path, error);
            if (error)
            {
                std::filesystem::remove(temporary, error);
                if (!std::filesystem::exists(path)) throw std::runtime_error("Cannot publish " + path.string());
            }
        }

        std::vector<std::uint32_t> ReadSpirv(const std::filesystem::path &path)
        {
            const auto bytes = ReadFile(path);
            constexpr std::uint32_t kSpirvMagic = 0x07230203u;
            std::vector<std::uint32_t> words(bytes.size() / sizeof(std::uint32_t));
            if (words.empty() || bytes.size() % sizeof(std::uint32_t) != 0) return {};
            std::copy_n(bytes.data(), bytes.size(), reinterpret_cast<char *>(words.data()));
            return words.front() == kSpirvMagic ? words : std::vector<std::uint32_t>{};
        }
    }

    struct ShaderGraphVariantCache::State
    {
        Options options;
        std::unique_ptr<ShaderGraphVariantCompiler> compiler;

        struct Job
        {
            VariantKey key;
            ShaderGraphProgramData data;
        };
        mutable std::mutex mutex;
        std::condition_variable wake, idle;
        std::unordered_map<VariantKey, Code, VariantKeyHash> resolved;
        std::unordered_set<VariantKey, VariantKeyHash> scheduled;
        std::deque<Job> queue;
        std::size_t active = 0;
        bool stopping = false;
        Stats stats;
        std::thread worker;

        // Worker thread only.
        std::filesystem::path packageDirectory;

        void Run()
        {
            for (;;)
            {
                Job job;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [&] { return stopping || !queue.empty(); });
                    if (stopping) return;
                    job = std::move(queue.front());
                    queue.pop_front();
                    ++active;
                }
                Code code = Resolve(job);
                {
                    std::lock_guard lock(mutex);
                    resolved[job.key] = std::move(code);
                    --active;
                }
                idle.notify_all();
            }
        }

        // Cache entries are keyed by everything that affects the SPIR-V: the
        // shader package sources, the generator and the compiler build.
        const std::filesystem::path &PackageDirectory()
        {
            if (packageDirectory.empty())
            {
                std::uint64_t hash = 1469598103934665603ull;
                Mix(hash, std::to_string(kShaderGraphCodegenVersion));
                Mix(hash, compiler->Identity());
                std::vector<std::filesystem::path> files;
                for (const auto &entry : std::filesystem::recursive_directory_iterator(options.sourceRoot))
                    if (entry.is_regular_file()) files.push_back(entry.path());
                std::sort(files.begin(), files.end());
                for (const auto &file : files)
                {
                    Mix(hash, std::filesystem::relative(file, options.sourceRoot).generic_string());
                    Mix(hash, ReadFile(file));
                }
                packageDirectory = options.cacheDirectory / Hex(hash);
            }
            return packageDirectory;
        }

        Code Resolve(const Job &job)
        {
            const std::string_view entryPoint = ShaderGraphVariantEntryPoint(job.key.stage);
            try
            {
                const auto directory = PackageDirectory() / Hex(job.key.structure);
                const auto binary = directory / (std::string(entryPoint) + ".spv");
                if (std::filesystem::exists(binary))
                    if (auto words = ReadSpirv(binary); !words.empty())
                    {
                        std::lock_guard lock(mutex);
                        ++stats.diskHits;
                        return std::make_shared<const std::vector<std::uint32_t>>(std::move(words));
                    }
                std::filesystem::create_directories(directory);
                // Kept beside the binary for inspection and compiler diagnostics.
                WriteFileAtomically(directory / "ShaderGraphGenerated.slang", GenerateShaderGraphSlang(job.data));
                auto words = compiler->CompileFragment(
                    {.variantDirectory = directory, .sourceDirectory = options.sourceRoot / "shaders", .entryPoint = entryPoint});
                WriteFileAtomically(binary, {reinterpret_cast<const char *>(words.data()), words.size() * sizeof(std::uint32_t)});
                std::lock_guard lock(mutex);
                ++stats.compiled;
                return std::make_shared<const std::vector<std::uint32_t>>(std::move(words));
            }
            catch (const std::exception &error)
            {
                std::cerr << "[ShaderGraph] Variant " << Hex(job.key.structure) << ' ' << entryPoint
                          << " is unavailable; keeping the interpreter. " << error.what() << '\n';
                std::lock_guard lock(mutex);
                ++stats.failures;
                return {};
            }
        }
    };

    ShaderGraphVariantCache::ShaderGraphVariantCache(Options options, std::unique_ptr<ShaderGraphVariantCompiler> compiler)
        : m_state(std::make_unique<State>())
    {
        if (!compiler) throw std::invalid_argument("ShaderGraphVariantCache requires a compiler");
        m_state->options = std::move(options);
        m_state->compiler = std::move(compiler);
        m_state->worker = std::thread([state = m_state.get()] { state->Run(); });
    }

    ShaderGraphVariantCache::~ShaderGraphVariantCache()
    {
        {
            std::lock_guard lock(m_state->mutex);
            m_state->stopping = true;
        }
        m_state->wake.notify_all();
        m_state->worker.join();
    }

    std::shared_ptr<ShaderGraphVariantCache> ShaderGraphVariantCache::ForShaderPackage(const std::filesystem::path &shaderRoot)
    {
        if (const char *setting = std::getenv("PLUTOGE_SHADER_GRAPH_VARIANTS"); setting && std::string_view(setting) == "0")
            return {};
        const auto sourceRoot = shaderRoot / "source";
        std::error_code error;
        if (!std::filesystem::is_regular_file(sourceRoot / "shaders" / "BasicLit.slang", error))
            return {};
        static std::mutex registryMutex;
        static std::map<std::filesystem::path, std::weak_ptr<ShaderGraphVariantCache>> registry;
        std::lock_guard lock(registryMutex);
        auto &slot = registry[std::filesystem::weakly_canonical(sourceRoot, error)];
        if (auto existing = slot.lock()) return existing;
        auto compiler = CreateSlangShaderGraphCompiler();
        if (!compiler) return {};
        auto cache = std::make_shared<ShaderGraphVariantCache>(
            Options{.sourceRoot = sourceRoot,
                    .cacheDirectory = std::filesystem::temp_directory_path() / "PlutoGE" / "ShaderGraphVariants"},
            std::move(compiler));
        slot = cache;
        return cache;
    }

    ShaderGraphVariantCache::Code ShaderGraphVariantCache::Find(const ShaderGraphProgram &program, ShaderGraphVariantStage stage)
    {
        // An empty program costs nothing to interpret.
        if (program.data.header.x <= 0) return {};
        const VariantKey key{ShaderGraphStructureHash(program), stage};
        std::lock_guard lock(m_state->mutex);
        if (const auto found = m_state->resolved.find(key); found != m_state->resolved.end())
            return found->second;
        if (m_state->scheduled.insert(key).second)
        {
            m_state->queue.push_back({key, program.data});
            m_state->wake.notify_one();
        }
        return {};
    }

    void ShaderGraphVariantCache::WaitIdle()
    {
        std::unique_lock lock(m_state->mutex);
        m_state->idle.wait(lock, [&] { return m_state->queue.empty() && m_state->active == 0; });
    }

    ShaderGraphVariantCache::Stats ShaderGraphVariantCache::GetStats() const
    {
        std::lock_guard lock(m_state->mutex);
        return m_state->stats;
    }
}
