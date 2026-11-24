#include "PlutoGE/render/Texture.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/TextureManager.h"

#include <algorithm>

namespace PlutoGE::render
{
    Texture *Texture::LoadFromFile(const char *filePath, TextureColorSpace colorSpace)
    {
        auto &engine = PlutoGE::core::Engine::GetInstance();
        Texture *texture = engine.GetTextureManager().LoadTextureFromFile(filePath, colorSpace);
        if (texture == nullptr)
        {
            delete texture;
            return nullptr; // Failed to load texture
        }

        return texture;
    }

    Texture *Texture::CpuColorCubemap(int resolution, std::span<const float> pixels)
    {
        if (resolution <= 0 || pixels.size() != static_cast<std::size_t>(resolution) * resolution * 24) return nullptr;
        auto *texture = new Texture(TextureConfig{});
        texture->m_kind = TextureKind::Cubemap;
        texture->m_width = texture->m_height = resolution;
        texture->m_channels = 4;
        texture->SetCubemapPixels(pixels);
        return texture;
    }

    std::unique_ptr<Texture> Texture::CreateRgba8(int width, int height, std::span<const unsigned char> pixels, std::string name)
    {
        if (width <= 0 || height <= 0 || pixels.size() != static_cast<std::size_t>(width) * height * 4) return nullptr;
        auto texture = std::make_unique<Texture>(TextureConfig{std::move(name)});
        texture->m_width = width;
        texture->m_height = height;
        texture->m_channels = 4;
        texture->ReplaceRgba8Pixels({pixels.begin(), pixels.end()});
        return texture;
    }

    // ---- CPU pixels ---------------------------------------------------------------------------------------------

    std::shared_ptr<std::vector<unsigned char>> Texture::ResidentPixelsLocked() const
    {
        if (!m_rgba8Pixels && m_pixelSource && m_kind == TextureKind::Texture2D)
            if (auto pixels = m_pixelSource(); pixels && pixels->size() == static_cast<std::size_t>(m_width) * m_height * 4)
                m_rgba8Pixels = std::make_shared<std::vector<unsigned char>>(std::move(*pixels));
        return m_rgba8Pixels;
    }

    std::span<const unsigned char> Texture::GetRgba8Pixels() const
    {
        std::lock_guard lock(m_pixelMutex);
        const auto pixels = ResidentPixelsLocked();
        return pixels ? std::span<const unsigned char>(*pixels) : std::span<const unsigned char>{};
    }

    std::shared_ptr<const std::vector<unsigned char>> Texture::AcquireRgba8Pixels() const
    {
        std::lock_guard lock(m_pixelMutex);
        return ResidentPixelsLocked();
    }

    std::vector<std::byte> Texture::CopyRgba8Bytes() const
    {
        const auto pixels = AcquireRgba8Pixels();
        if (!pixels)
            return {};
        const auto *bytes = reinterpret_cast<const std::byte *>(pixels->data());
        return {bytes, bytes + pixels->size()};
    }

    bool Texture::ReleaseUploadedPixels()
    {
        if (!CanReleasePixels() || !IsUploaded())
            return false;
        std::lock_guard lock(m_pixelMutex);
        if (!m_rgba8Pixels)
            return false;
        // Readers on other threads keep their shared copies until they finish.
        m_rgba8Pixels.reset();
        return true;
    }

    bool Texture::HasResidentPixels() const
    {
        std::lock_guard lock(m_pixelMutex);
        return m_rgba8Pixels != nullptr;
    }

    void Texture::ReplaceRgba8Pixels(std::vector<unsigned char> pixels)
    {
        auto replacement = pixels.empty() ? nullptr : std::make_shared<std::vector<unsigned char>>(std::move(pixels));
        std::lock_guard lock(m_pixelMutex);
        m_rgba8Pixels = std::move(replacement);
    }

    bool Texture::SetRgba8Pixels(std::span<const unsigned char> pixels)
    {
        if (m_kind != TextureKind::Texture2D || pixels.size() != static_cast<std::size_t>(m_width) * m_height * 4) return false;
        ReplaceRgba8Pixels({pixels.begin(), pixels.end()});
        ++m_contentRevision;
        return true;
    }

    bool Texture::UpdateRgba8Region(const TextureRect &rect, std::span<const unsigned char> pixels)
    {
        if (m_kind != TextureKind::Texture2D || rect.Empty() || rect.x < 0 || rect.y < 0 ||
            rect.x + rect.width > m_width || rect.y + rect.height > m_height ||
            pixels.size() != static_cast<std::size_t>(rect.width) * rect.height * 4)
            return false;
        // Painted in place, as painting needs (whole replacements would copy the texture every dab).
        std::lock_guard lock(m_pixelMutex);
        const auto target = ResidentPixelsLocked();
        if (!target)
            return false;
        const std::size_t row = static_cast<std::size_t>(rect.width) * 4;
        for (int y = 0; y < rect.height; ++y)
            std::copy_n(pixels.data() + y * row, row,
                        target->data() + (static_cast<std::size_t>(rect.y + y) * m_width + rect.x) * 4);
        ++m_contentRevision;
        constexpr std::size_t kMaxRegionUpdates = 64;
        if (m_regionUpdates.size() == kMaxRegionUpdates)
            m_regionUpdates.erase(m_regionUpdates.begin());
        m_regionUpdates.emplace_back(m_contentRevision, rect);
        return true;
    }

    std::optional<TextureRect> Texture::GetChangedRectSince(std::uint64_t revision) const
    {
        if (revision >= m_contentRevision)
            return TextureRect{};
        // Every revision after `revision` must be a recorded region update; any other change (a full replacement,
        // a reload) bumps the revision without a record, so the counts differ.
        TextureRect changed;
        std::uint64_t covered = 0;
        for (const auto &[updateRevision, rect] : m_regionUpdates)
        {
            if (updateRevision <= revision)
                continue;
            ++covered;
            if (changed.Empty())
                changed = rect;
            else
            {
                const int x1 = std::max(changed.x + changed.width, rect.x + rect.width);
                const int y1 = std::max(changed.y + changed.height, rect.y + rect.height);
                changed.x = std::min(changed.x, rect.x);
                changed.y = std::min(changed.y, rect.y);
                changed.width = x1 - changed.x;
                changed.height = y1 - changed.y;
            }
        }
        if (covered != m_contentRevision - revision)
            return std::nullopt;
        return changed;
    }

    bool Texture::SetCubemapPixels(std::span<const float> pixels)
    {
        if (m_kind != TextureKind::Cubemap || pixels.size() != static_cast<std::size_t>(m_width) * m_height * 24) return false;
        m_cubemapPixels.assign(pixels.begin(), pixels.end());
        ++m_contentRevision;
        return true;
    }
}
