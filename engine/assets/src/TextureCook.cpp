#include "PlutoGE/assets/TextureCook.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/ImageDecoding.h"
#include "PlutoGE/render/MaterialTextureSlots.h"
#include "PlutoGE/render/TextureCooking.h"

#include <algorithm>
#include <fstream>
#include <future>
#include <map>
#include <set>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace PlutoGE::assets
{
    namespace
    {
        using Sampling = std::pair<render::TextureSemantic, bool>; // semantic, sRGB

        struct TextureJob
        {
            std::string path;
            std::set<Sampling> samplings;
        };

        struct JobResult
        {
            bool readable = false;
            std::size_t encoded = 0, alreadyCooked = 0, uncompressed = 0, failed = 0;
        };

        // A material file's texture slots (resolved to files) and how each is sampled, as material loading reads them.
        void CollectMaterialTextures(const std::filesystem::path &file, const AssetManager &resolver,
                                     std::map<std::string, std::set<Sampling>> &textures)
        {
            std::ifstream input(file, std::ios::binary);
            std::vector<std::pair<render::MaterialTextureSlot, std::string>> slots;
            bool emissionChannelMask = false;
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const auto delimiter = line.find('=');
                if (delimiter == std::string::npos) continue;
                const std::string_view key(line.data(), delimiter);
                const std::string value = line.substr(delimiter + 1);
                if (key == "EmissionChannelMask")
                    emissionChannelMask = value == "true" || value == "1";
                else if (const auto *slot = render::FindMaterialTextureSlotByAssetKey(key); slot && !value.empty())
                    slots.emplace_back(slot->slot, value);
            }
            for (const auto &[slot, value] : slots)
            {
                const auto path = resolver.ResolveMaterialTexturePath(value);
                std::error_code error;
                if (path.empty() || !std::filesystem::is_regular_file(path, error))
                    continue;
                const auto sampling = render::GetMaterialTextureSampling(slot, emissionChannelMask);
                textures[path].insert({sampling.semantic, sampling.srgb});
            }
        }

        JobResult Cook(const TextureJob &job, const std::filesystem::path &store, const std::filesystem::path &projectRoot)
        {
            JobResult result;
            const auto image = render::DecodeImageFile(job.path);
            if (!image)
                return result;
            result.readable = true;
            const auto rgba8 = std::as_bytes(std::span(image->rgba8));
            // Recorded in the store's source index, so the game loads the texture without decoding it.
            const render::TextureCookSource source{job.path, projectRoot, image->channels};
            for (const auto &[semantic, srgb] : job.samplings)
                switch (render::CookCompressedTexture(store, rgba8, static_cast<std::uint32_t>(image->width),
                                                      static_cast<std::uint32_t>(image->height), semantic, srgb, {},
                                                      render::DesktopBlockCompressionSupport(), &source))
                {
                case render::TextureCookOutcome::Encoded: ++result.encoded; break;
                case render::TextureCookOutcome::AlreadyCooked: ++result.alreadyCooked; break;
                case render::TextureCookOutcome::Uncompressed: ++result.uncompressed; break;
                case render::TextureCookOutcome::Failed: ++result.failed; break;
                }
            return result;
        }
    }

    bool CookMaterialTextures(const std::filesystem::path &projectRoot, const std::string &assetDirectory,
                              std::uint32_t assetPipelineVersion, TextureCookStats *stats, std::string *errorMessage)
    {
        TextureCookStats totals;
        // Resolves texture references exactly as the exported game will: its asset directory and runtime catalog.
        AssetManager resolver;
        resolver.SetProjectContext(projectRoot.string(), assetDirectory, assetPipelineVersion);
        if (const auto catalog = projectRoot / "PlutoAssetCatalog.manifest"; std::filesystem::exists(catalog))
            resolver.LoadAssetCatalog(catalog.string());

        std::map<std::string, std::set<Sampling>> textures;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(projectRoot, error), end; !error && it != end; it.increment(error))
            if (it->is_regular_file() && it->path().extension() == ".plutomaterial")
            {
                ++totals.materials;
                CollectMaterialTextures(it->path(), resolver, textures);
            }
        totals.textures = textures.size();

        const auto store = projectRoot / render::kCookedTextureDirectory;
        std::filesystem::create_directories(store, error);
        if (error)
        {
            if (errorMessage) *errorMessage = "Cannot create the cooked texture store: " + error.message();
            return false;
        }

        // Each file decodes once and encodes each sampling; files cook side by side, a few at a time.
        std::vector<TextureJob> jobs;
        jobs.reserve(textures.size());
        for (auto &[path, samplings] : textures)
            jobs.push_back({path, std::move(samplings)});
        const std::size_t workers = std::max<std::size_t>(1, std::thread::hardware_concurrency());
        std::vector<std::future<JobResult>> running;
        const auto collect = [&](std::future<JobResult> &future) {
            const auto result = future.get();
            if (!result.readable) ++totals.unreadable;
            totals.encoded += result.encoded;
            totals.alreadyCooked += result.alreadyCooked;
            totals.uncompressed += result.uncompressed;
            totals.failed += result.failed;
        };
        for (const auto &job : jobs)
        {
            if (running.size() == workers)
            {
                collect(running.front());
                running.erase(running.begin());
            }
            running.push_back(std::async(std::launch::async, [&job, &store, &projectRoot] { return Cook(job, store, projectRoot); }));
        }
        for (auto &future : running)
            collect(future);

        if (stats) *stats = totals;
        if (totals.failed > 0)
        {
            if (errorMessage) *errorMessage = "Could not store " + std::to_string(totals.failed) + " cooked texture(s) in " + store.string();
            return false;
        }
        return true;
    }
}
