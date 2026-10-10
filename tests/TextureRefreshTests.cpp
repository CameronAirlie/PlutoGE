#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/platform/Window.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace PlutoGE;
namespace
{
    void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        std::filesystem::path parent = std::filesystem::temp_directory_path();
        std::filesystem::path root = parent / ("PlutoGE-texture-refresh-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch()
        {
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-texture-refresh-"))
            { std::error_code error; std::filesystem::remove_all(root, error); }
        }
    };
    void WriteImage(const std::filesystem::path &path, unsigned char red, unsigned char green, unsigned char blue)
    {
        std::array<unsigned char, 58> bytes{};
        bytes[0]='B'; bytes[1]='M'; bytes[2]=58; bytes[10]=54; bytes[14]=40;
        bytes[18]=1; bytes[22]=1; bytes[26]=1; bytes[28]=24; bytes[34]=4;
        bytes[54]=blue; bytes[55]=green; bytes[56]=red;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); output.close();
        Require(bool(output), "Could not write image fixture");
    }
    void Pixels(const render::Texture &texture, unsigned char red, unsigned char green, unsigned char blue)
    {
        const auto pixels = texture.GetRgba8Pixels();
        Require(pixels.size() == 4 && pixels[0] == red && pixels[1] == green && pixels[2] == blue && pixels[3] == 255,
            "Refreshed source pixels differ from authored bytes");
    }
}
int main(int argc, char **argv)
try
{
    const bool gpu = argc > 1 && std::string_view(argv[1]) == "opengl";
    platform::Window window;
    if (gpu) Require(window.Create({.title="Texture refresh", .width=64, .height=64, .visible=false}) &&
        window.EnsureOpenGLContextCurrent(true), "Could not create OpenGL fixture context");
    Scratch scratch;
    std::filesystem::create_directories(scratch.root);
    const auto path = scratch.root / "Authored.bmp";
    WriteImage(path, 100, 150, 200);
    std::weak_ptr<const void> linearLifetime, srgbLifetime;
    {
        render::TextureManager textures(true);
        if (gpu) textures.SetWindow(&window);
        auto *linear = textures.LoadTextureFromFile(path.string().c_str(), render::TextureColorSpace::Linear);
        auto *srgb = textures.LoadTextureFromFile(path.string().c_str(), render::TextureColorSpace::SRGB);
        Require(linear && srgb && linear != srgb, "Colour-space caches were not independent");
        linearLifetime = linear->GetLifetimeToken(); srgbLifetime = srgb->GetLifetimeToken();
        const auto identity = linear->GetIdentity(), srgbIdentity = srgb->GetIdentity();
        const auto revision = linear->GetContentRevision(), srgbRevision = srgb->GetContentRevision();
        const auto oldLinearGpu = linear->GetTextureID(), oldSrgbGpu = srgb->GetTextureID();
        render::Material borrowed({.albedoTexture=srgb, .normalTexture=linear});
        WriteImage(path, 20, 40, 60);
        std::string error;
        Require(textures.ReloadFileTexture(path.string(), &error), error.c_str());
        Require(textures.LoadTextureFromFile(path.string().c_str()) == linear &&
            textures.LoadTextureFromFile(path.string().c_str(), render::TextureColorSpace::SRGB) == srgb &&
            linear->GetIdentity() == identity && srgb->GetIdentity() == srgbIdentity &&
            linear->GetContentRevision() == revision + 1 && srgb->GetContentRevision() == srgbRevision + 1 &&
            borrowed.ReadConfig().normalTexture == linear && borrowed.ReadConfig().albedoTexture == srgb,
            "Refresh invalidated a borrower or did not publish content revisions");
        Pixels(*linear,20,40,60); Pixels(*srgb,20,40,60);
        if (gpu)
        {
            Require(linear->GetTextureID() && srgb->GetTextureID() && !glIsTexture(oldLinearGpu) && !glIsTexture(oldSrgbGpu),
                "Previous GPU allocations were not retired");
            std::array<unsigned char,4> readback{};
            glBindTexture(GL_TEXTURE_2D, linear->GetTextureID());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
            Require(readback == std::array<unsigned char,4>{20,40,60,255}, "OpenGL refresh did not upload new pixels");
        }
        const auto validRevision = linear->GetContentRevision(), validSrgbRevision = srgb->GetContentRevision();
        { std::ofstream invalid(path, std::ios::binary | std::ios::trunc); invalid << "invalid image"; }
        Require(!textures.ReloadFileTexture(path.string(), &error) && !error.empty() &&
            linear->GetContentRevision() == validRevision && srgb->GetContentRevision() == validSrgbRevision,
            "Invalid image replaced the accepted colour-space pair");
        Pixels(*linear,20,40,60); Pixels(*srgb,20,40,60);
        std::filesystem::remove(path);
        Require(!textures.ReloadFileTexture(path.string(), &error) && linear->GetContentRevision() == validRevision,
            "Missing image changed its accepted texture");
    }
    Require(linearLifetime.expired() && srgbLifetime.expired(), "Owning texture cache leaked refreshed resources");
    if (gpu) window.Close();
    std::cout << "Stable texture refresh checks passed.\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
