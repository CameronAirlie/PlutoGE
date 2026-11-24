#pragma once
#include "PlutoGE/render/RenderObjectIdentity.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    enum class TextureColorSpace : std::uint8_t
    {
        Linear = 0,
        SRGB = 1,
    };

    // The shape of a texture's CPU-owned pixels: RGBA8 2D (GetRgba8Pixels) or
    // linear HDR cube faces (GetCubemapPixels).
    enum class TextureKind : std::uint8_t
    {
        Texture2D = 0,
        Cubemap = 1,
    };

    // A rectangle of texels.
    struct TextureRect
    {
        int x = 0, y = 0, width = 0, height = 0;
        [[nodiscard]] bool Empty() const { return width <= 0 || height <= 0; }
    };

    // Identifies one version of a source file without reading it: its size and modification time.
    struct TextureFileVersion
    {
        std::uint64_t size = 0;
        std::int64_t modified = 0; // file clock ticks; 0 where there is none (files inside content packs)
        friend bool operator==(const TextureFileVersion &, const TextureFileVersion &) = default;
    };

    struct TextureConfig
    {
        std::string filePath; // Path to the texture file
    };

    class Texture
    {
    public:
        Texture(const TextureConfig &config) : m_filePath(config.filePath) {}
        // Virtual so specialised textures (such as RenderTexture) can be owned
        // and identified through Texture pointers held by materials.
        virtual ~Texture() = default;

        std::uint64_t GetIdentity() const { return m_identity.Value(); }
        std::uint64_t GetContentRevision() const { return m_contentRevision; }
        std::weak_ptr<const void> GetLifetimeToken() const { return m_lifetime; }
        TextureKind GetKind() const { return m_kind; }

        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }
        int GetChannels() const { return m_channels; }
        const std::string &GetFilePath() const { return m_filePath; }
        // The version of GetFilePath() this texture's content is, while it is exactly that file's (no pixels set
        // or painted since it was loaded): what TextureSourceIndex knows the file by. nullopt otherwise.
        [[nodiscard]] std::optional<TextureFileVersion> GetFileVersion() const
        {
            return m_fileVersion && m_fileContentRevision == m_contentRevision ? m_fileVersion : std::nullopt;
        }

        // ---- CPU pixels ----------------------------------------------------------------------------------------
        // A file texture's RGBA8 pixels may be released once the renderers hold GPU copies of them (MarkUploaded and
        // TextureManager's pixel retention), and come back from the file when read again. Textures without a file
        // (CreateRgba8, generated or painted data) always keep theirs.
        //
        // Owner thread. Reads the pixels back first if they were released; the span stays valid until the owner next
        // replaces or releases them. Empty without pixels.
        [[nodiscard]] std::span<const unsigned char> GetRgba8Pixels() const;
        // Any thread. Shared ownership keeps the pixels alive while the caller reads them, whatever the owner does
        // meanwhile. Null without pixels.
        [[nodiscard]] std::shared_ptr<const std::vector<unsigned char>> AcquireRgba8Pixels() const;
        // Any thread. A byte copy, as renderers upload them (their texture pixel reader). Empty without pixels.
        [[nodiscard]] std::vector<std::byte> CopyRgba8Bytes() const;
        // A renderer has a GPU copy of the current content and will not read the CPU pixels again unless the content
        // changes. Any thread.
        void MarkUploaded() const noexcept { m_uploadedRevision.store(m_contentRevision, std::memory_order_release); }
        [[nodiscard]] bool IsUploaded() const noexcept
        {
            return m_uploadedRevision.load(std::memory_order_acquire) == m_contentRevision;
        }
        // Whether released pixels can be read again (the texture has a pixel source).
        [[nodiscard]] bool CanReleasePixels() const noexcept { return static_cast<bool>(m_pixelSource); }
        // Owner thread. Drops the pixels of an uploaded texture that can read them again; true when it did.
        bool ReleaseUploadedPixels();
        [[nodiscard]] bool HasResidentPixels() const;
        // Reads the pixels again after a release (a file texture's decode): `width` x `height` RGBA8, or nothing.
        using PixelSource = std::function<std::optional<std::vector<unsigned char>>()>;
        // Owner thread, when the texture is made.
        void SetPixelSource(PixelSource source) { m_pixelSource = std::move(source); }

        [[nodiscard]] std::span<const float> GetCubemapPixels() const noexcept { return m_cubemapPixels; }
        // Linear HDR faces in +X,-X,+Y,-Y,+Z,-Z order, bottom-up within each face.
        static Texture *CpuColorCubemap(int resolution, std::span<const float> pixels);
        bool SetCubemapPixels(std::span<const float> pixels);
        // A CPU-owned RGBA8 2D texture: the RHI renderers upload its pixels and re-upload them
        // after SetRgba8Pixels, which keeps the size. Used for generated data such as terrain weight maps.
        static std::unique_ptr<Texture> CreateRgba8(int width, int height, std::span<const unsigned char> pixels,
                                                    std::string name = {});
        bool SetRgba8Pixels(std::span<const unsigned char> pixels);
        // Replaces a rectangle of a CPU-owned RGBA8 texture (`pixels` packed for the rectangle). Renderers already
        // holding the texture upload just that rectangle (GetChangedRectSince).
        bool UpdateRgba8Region(const TextureRect &rect, std::span<const unsigned char> pixels);
        // The rectangle covering every change after `revision` when all of them were region updates (empty when
        // nothing changed); nullopt when any change replaced the whole texture, so it must be read again in full.
        [[nodiscard]] std::optional<TextureRect> GetChangedRectSince(std::uint64_t revision) const;

        static Texture *LoadFromFile(const char *filePath, TextureColorSpace colorSpace = TextureColorSpace::Linear);

    protected:
        // Replaces the pixels (owner thread); they must be `m_width` x `m_height` RGBA8 or empty.
        void ReplaceRgba8Pixels(std::vector<unsigned char> pixels);
        // The resident pixels, reading them back from the pixel source when released. Holds m_pixelMutex.
        [[nodiscard]] std::shared_ptr<std::vector<unsigned char>> ResidentPixelsLocked() const;

        RenderObjectIdentity m_identity;
        mutable std::uint64_t m_contentRevision = 1;
        std::shared_ptr<const void> m_lifetime = std::make_shared<const int>(0);
        friend class TextureManager;   // Allow TextureManager to access private members
        std::string m_filePath;        // Path to the texture file (for reference)
        TextureKind m_kind = TextureKind::Texture2D;
        int m_width = 0;
        int m_height = 0;
        int m_channels = 0; // Number of color channels (e.g., 3 for RGB, 4 for RGBA)
        // The decoded RGBA8 pixels, null when there are none or they were released. Shared, so a reader on another
        // thread (AcquireRgba8Pixels) keeps what it read alive; swapped under m_pixelMutex.
        mutable std::shared_ptr<std::vector<unsigned char>> m_rgba8Pixels;
        PixelSource m_pixelSource;
        mutable std::mutex m_pixelMutex;
        // The content revision a renderer last reported holding on the GPU (MarkUploaded); 0 for none.
        mutable std::atomic<std::uint64_t> m_uploadedRevision{0};
        // The file version the content was loaded from, and the content revision that load produced (TextureManager).
        std::optional<TextureFileVersion> m_fileVersion;
        std::uint64_t m_fileContentRevision = 0;
        // Recent region updates (revision, rectangle), for GetChangedRectSince; bounded.
        std::vector<std::pair<std::uint64_t, TextureRect>> m_regionUpdates;
        std::vector<float> m_cubemapPixels;
    };
}
