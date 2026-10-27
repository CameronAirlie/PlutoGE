#include "PlutoGE/scene/ui/UIEngineServices.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/ui/text/FontLibrary.h"

#include <unordered_map>

namespace PlutoGE::scene
{
    namespace
    {
        class EngineTextureInfo final : public IUITextureInfo
        {
        public:
            glm::vec2 GetTextureSize(const std::string &reference) override
            {
                auto &entry = m_cache[reference];
                // Load once; reload only when a loaded texture was released
                // (reimport, project change). Missing textures are not retried.
                if (!entry.attempted || (entry.texture && entry.lifetime.expired()))
                {
                    entry.attempted = true;
                    entry.texture = LoadUITexture(reference);
                    entry.lifetime = entry.texture ? entry.texture->GetLifetimeToken() : std::weak_ptr<const void>{};
                }
                return entry.texture ? glm::vec2(entry.texture->GetWidth(), entry.texture->GetHeight()) : glm::vec2(0.0f);
            }

        private:
            struct Entry
            {
                render::Texture *texture = nullptr;
                std::weak_ptr<const void> lifetime;
                bool attempted = false;
            };
            std::unordered_map<std::string, Entry> m_cache;
        };
    }

    std::filesystem::path ResolveUIAssetPath(const std::string &reference)
    {
        if (reference.empty())
            return {};
        const std::string resolved = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference);
        return resolved.empty() ? std::filesystem::path(reference) : std::filesystem::path(resolved);
    }

    render::Texture *LoadUITexture(const std::string &reference)
    {
        const auto path = ResolveUIAssetPath(reference);
        return path.empty() ? nullptr : render::Texture::LoadFromFile(path.string().c_str());
    }

    std::shared_ptr<IUITextureInfo> CreateEngineTextureInfo()
    {
        return std::make_shared<EngineTextureInfo>();
    }

    void ConfigureUIFontsForEngine()
    {
        static const bool configured = []
        {
            auto &fonts = ui::FontLibrary::Get();
            fonts.SetPathResolver([](const std::string &reference) { return ResolveUIAssetPath(reference); });
            // Exported games keep project fonts in the content pack.
            fonts.SetFileReader([](const std::filesystem::path &path)
            {
                std::string bytes;
                if (!content::ReadFile(path, bytes))
                    return std::vector<std::byte>{};
                const auto *data = reinterpret_cast<const std::byte *>(bytes.data());
                return std::vector<std::byte>(data, data + bytes.size());
            });
            return true;
        }();
        (void)configured;
    }
}
