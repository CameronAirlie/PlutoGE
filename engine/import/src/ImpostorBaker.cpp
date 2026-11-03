#include "PlutoGE/import/ImpostorBaker.h"

#include "PlutoGE/import/MeshImporter.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace PlutoGE::assetimport
{
    namespace
    {
        namespace layout = render::impostor;

        glm::vec4 SampleBilinear(const ImpostorImage &image, glm::vec2 uv)
        {
            // Repeat addressing, as glTF samplers default to.
            const glm::vec2 texel = (uv - glm::floor(uv)) * glm::vec2(image.width, image.height) - 0.5f;
            const glm::vec2 base = glm::floor(texel), weight = texel - base;
            const auto fetch = [&](int x, int y)
            {
                x = ((x % static_cast<int>(image.width)) + static_cast<int>(image.width)) % static_cast<int>(image.width);
                y = ((y % static_cast<int>(image.height)) + static_cast<int>(image.height)) % static_cast<int>(image.height);
                const auto *pixel = &image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4];
                return glm::vec4(pixel[0], pixel[1], pixel[2], pixel[3]) / 255.0f;
            };
            const int x = static_cast<int>(base.x), y = static_cast<int>(base.y);
            return glm::mix(glm::mix(fetch(x, y), fetch(x + 1, y), weight.x),
                            glm::mix(fetch(x, y + 1), fetch(x + 1, y + 1), weight.x), weight.y);
        }

        // The finest-LOD triangles of every submesh, with their materials.
        struct BakeTriangle
        {
            std::array<std::uint32_t, 3> vertices;
            const ImpostorMaterial *material;
        };

        // One frame's samples: depth toward the viewer (normalized by the radius), colour and normal, at
        // frameResolution * supersampling on a side. Uncovered samples keep depth = -inf.
        struct FrameSamples
        {
            std::uint32_t size = 0;
            std::vector<float> depth;
            std::vector<glm::vec4> color;
            std::vector<glm::vec3> normal;
        };

        void RasterizeFrame(const render::MeshData &mesh, std::span<const BakeTriangle> triangles,
                            const layout::Parameters &parameters, const layout::FrameBasis &basis, FrameSamples &frame)
        {
            const float size = static_cast<float>(frame.size);
            std::vector<glm::vec3> projected(mesh.vertices.size());
            for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
            {
                const auto &position = mesh.vertices[index].position;
                const glm::vec3 offset = (glm::vec3(position[0], position[1], position[2]) - parameters.boundsCenter) /
                                         parameters.boundsRadius;
                // Pixel x grows with right, pixel y with -up (rows top first); z is toward the viewer.
                projected[index] = {(glm::dot(offset, basis.right) * 0.5f + 0.5f) * size,
                                    (0.5f - glm::dot(offset, basis.up) * 0.5f) * size, glm::dot(offset, basis.view)};
            }
            for (const auto &triangle : triangles)
            {
                const glm::vec3 a = projected[triangle.vertices[0]], b = projected[triangle.vertices[1]],
                                c = projected[triangle.vertices[2]];
                const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
                if (std::abs(area) < 1e-12f) continue;
                const int minX = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
                const int maxX = std::min(static_cast<int>(frame.size) - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
                const int minY = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
                const int maxY = std::min(static_cast<int>(frame.size) - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
                const auto &material = *triangle.material;
                for (int y = minY; y <= maxY; ++y)
                    for (int x = minX; x <= maxX; ++x)
                    {
                        const glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                        // Barycentric weights from edge functions; either winding is drawn.
                        const float w0 = ((b.x - p.x) * (c.y - p.y) - (b.y - p.y) * (c.x - p.x)) / area;
                        const float w1 = ((c.x - p.x) * (a.y - p.y) - (c.y - p.y) * (a.x - p.x)) / area;
                        const float w2 = 1.0f - w0 - w1;
                        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                        const float depth = w0 * a.z + w1 * b.z + w2 * c.z;
                        const std::size_t sample = static_cast<std::size_t>(y) * frame.size + x;
                        if (depth <= frame.depth[sample]) continue;
                        const auto &va = mesh.vertices[triangle.vertices[0]], &vb = mesh.vertices[triangle.vertices[1]],
                                   &vc = mesh.vertices[triangle.vertices[2]];
                        glm::vec4 color = material.baseColor;
                        if (material.baseColorTexture)
                        {
                            const glm::vec2 uv = w0 * glm::vec2(va.uv[0], va.uv[1]) + w1 * glm::vec2(vb.uv[0], vb.uv[1]) +
                                                 w2 * glm::vec2(vc.uv[0], vc.uv[1]);
                            color *= SampleBilinear(*material.baseColorTexture, uv);
                        }
                        if (material.alphaTested && color.a < material.alphaCutoff) continue;
                        glm::vec3 normal = w0 * glm::vec3(va.normal[0], va.normal[1], va.normal[2]) +
                                           w1 * glm::vec3(vb.normal[0], vb.normal[1], vb.normal[2]) +
                                           w2 * glm::vec3(vc.normal[0], vc.normal[1], vc.normal[2]);
                        normal = glm::dot(normal, normal) > 1e-12f ? glm::normalize(normal) : basis.view;
                        if (material.twoSided && glm::dot(normal, basis.view) < 0.0f) normal = -normal;
                        frame.depth[sample] = depth;
                        frame.color[sample] = glm::vec4(glm::vec3(color), 1.0f);
                        frame.normal[sample] = normal;
                    }
            }
        }

        std::uint8_t Unorm8(float value)
        {
            return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }

        // Fills uncovered texels' colour and normal from covered neighbours (alpha stays 0), so filtering and mips
        // near the silhouette do not pull in black.
        void DilateUncovered(ImpostorAtlas &atlas, std::uint32_t passes)
        {
            const std::uint32_t size = atlas.albedo.width;
            for (std::uint32_t pass = 0; pass < passes; ++pass)
            {
                auto albedo = atlas.albedo.pixels, normal = atlas.normalDepth.pixels;
                std::vector<std::uint8_t> filled(std::size_t(size) * size);
                for (std::uint32_t y = 0; y < size; ++y)
                    for (std::uint32_t x = 0; x < size; ++x)
                    {
                        const std::size_t texel = std::size_t(y) * size + x;
                        filled[texel] = atlas.albedo.pixels[texel * 4 + 3] != 0 || atlas.albedo.pixels[texel * 4 + 0] != 0 ||
                                        atlas.albedo.pixels[texel * 4 + 1] != 0 || atlas.albedo.pixels[texel * 4 + 2] != 0;
                    }
                for (std::uint32_t y = 0; y < size; ++y)
                    for (std::uint32_t x = 0; x < size; ++x)
                    {
                        const std::size_t texel = std::size_t(y) * size + x;
                        if (filled[texel]) continue;
                        glm::vec3 color{0.0f}, direction{0.0f};
                        int count = 0;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                const int nx = static_cast<int>(x) + dx, ny = static_cast<int>(y) + dy;
                                if (nx < 0 || ny < 0 || nx >= static_cast<int>(size) || ny >= static_cast<int>(size)) continue;
                                const std::size_t neighbour = std::size_t(ny) * size + nx;
                                if (!filled[neighbour]) continue;
                                for (int channel = 0; channel < 3; ++channel)
                                {
                                    color[channel] += atlas.albedo.pixels[neighbour * 4 + channel];
                                    direction[channel] += atlas.normalDepth.pixels[neighbour * 4 + channel];
                                }
                                ++count;
                            }
                        if (count == 0) continue;
                        for (int channel = 0; channel < 3; ++channel)
                        {
                            albedo[texel * 4 + channel] = static_cast<std::uint8_t>(std::lround(color[channel] / count));
                            normal[texel * 4 + channel] = static_cast<std::uint8_t>(std::lround(direction[channel] / count));
                        }
                    }
                atlas.albedo.pixels = std::move(albedo);
                atlas.normalDepth.pixels = std::move(normal);
            }
        }
    }

    ImpostorAtlas BakeImpostor(const render::MeshData &mesh, std::span<const render::Submesh> submeshes,
                               std::span<const ImpostorMaterial> materials, const ImpostorBakeSettings &settings)
    {
        ImpostorAtlas atlas;
        static const ImpostorMaterial untextured{};
        std::vector<BakeTriangle> triangles;
        glm::vec3 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
        for (const auto &submesh : submeshes)
        {
            const auto *material = submesh.materialIndex < materials.size() ? &materials[submesh.materialIndex] : &untextured;
            if (submesh.indexOffset + static_cast<std::uint64_t>(submesh.indexCount) > mesh.indices.size()) continue;
            for (std::uint32_t index = 0; index + 2 < submesh.indexCount; index += 3)
            {
                const auto *corners = &mesh.indices[submesh.indexOffset + index];
                if (corners[0] >= mesh.vertices.size() || corners[1] >= mesh.vertices.size() || corners[2] >= mesh.vertices.size())
                    continue;
                triangles.push_back({{corners[0], corners[1], corners[2]}, material});
                for (const auto corner : {corners[0], corners[1], corners[2]})
                {
                    const auto &position = mesh.vertices[corner].position;
                    low = glm::min(low, glm::vec3(position[0], position[1], position[2]));
                    high = glm::max(high, glm::vec3(position[0], position[1], position[2]));
                }
            }
        }
        if (triangles.empty() || settings.framesPerSide < 2 || settings.frameResolution == 0) return atlas;

        auto &parameters = atlas.parameters;
        parameters.framesPerSide = settings.framesPerSide;
        parameters.frameResolution = settings.frameResolution;
        parameters.maxScreenRadiusPixels = settings.maxScreenRadiusPixels;
        parameters.boundsCenter = (low + high) * 0.5f;
        for (const auto &triangle : triangles)
            for (const auto corner : triangle.vertices)
            {
                const auto &position = mesh.vertices[corner].position;
                parameters.boundsRadius = std::max(parameters.boundsRadius,
                    glm::length(glm::vec3(position[0], position[1], position[2]) - parameters.boundsCenter));
            }
        if (parameters.boundsRadius <= 0.0f) return {};

        const std::uint32_t atlasSize = parameters.AtlasSize();
        for (auto *image : {&atlas.albedo, &atlas.normalDepth})
        {
            image->width = image->height = atlasSize;
            image->pixels.assign(std::size_t(atlasSize) * atlasSize * 4, 0);
        }
        const std::uint32_t supersampling = std::max(settings.supersampling, 1u);
        std::vector<std::uint32_t> frames(std::size_t(settings.framesPerSide) * settings.framesPerSide);
        std::iota(frames.begin(), frames.end(), 0u);
        std::for_each(std::execution::par, frames.begin(), frames.end(), [&](std::uint32_t frameIndex)
        {
            const glm::uvec2 frameCoordinates(frameIndex % settings.framesPerSide, frameIndex / settings.framesPerSide);
            const auto basis = layout::Basis(layout::FrameDirection(frameCoordinates, settings.framesPerSide));
            FrameSamples samples;
            samples.size = settings.frameResolution * supersampling;
            const std::size_t count = std::size_t(samples.size) * samples.size;
            samples.depth.assign(count, -std::numeric_limits<float>::infinity());
            samples.color.assign(count, glm::vec4(0.0f));
            samples.normal.assign(count, glm::vec3(0.0f));
            RasterizeFrame(mesh, triangles, parameters, basis, samples);
            // Resolve each pixel from its samples: coverage in alpha, the covered samples' mean colour, normal in
            // the frame basis and nearest depth.
            for (std::uint32_t y = 0; y < settings.frameResolution; ++y)
                for (std::uint32_t x = 0; x < settings.frameResolution; ++x)
                {
                    glm::vec3 color{0.0f}, normal{0.0f};
                    float depth = -std::numeric_limits<float>::infinity();
                    std::uint32_t covered = 0;
                    for (std::uint32_t sy = 0; sy < supersampling; ++sy)
                        for (std::uint32_t sx = 0; sx < supersampling; ++sx)
                        {
                            const std::size_t sample = std::size_t(y * supersampling + sy) * samples.size + x * supersampling + sx;
                            if (samples.color[sample].a == 0.0f) continue;
                            color += glm::vec3(samples.color[sample]);
                            normal += samples.normal[sample];
                            depth = std::max(depth, samples.depth[sample]);
                            ++covered;
                        }
                    const std::size_t texel = (std::size_t(frameCoordinates.y * settings.frameResolution + y) * atlasSize +
                                               frameCoordinates.x * settings.frameResolution + x) * 4;
                    if (covered == 0) continue;
                    color /= static_cast<float>(covered);
                    normal = glm::dot(normal, normal) > 1e-12f ? glm::normalize(normal) : basis.view;
                    const glm::vec3 local(glm::dot(normal, basis.right), glm::dot(normal, basis.up), glm::dot(normal, basis.view));
                    const float coverage = static_cast<float>(covered) / static_cast<float>(supersampling * supersampling);
                    auto *albedo = &atlas.albedo.pixels[texel];
                    auto *normalDepth = &atlas.normalDepth.pixels[texel];
                    albedo[0] = Unorm8(color.r); albedo[1] = Unorm8(color.g); albedo[2] = Unorm8(color.b);
                    albedo[3] = Unorm8(coverage);
                    normalDepth[0] = Unorm8(local.x * 0.5f + 0.5f); normalDepth[1] = Unorm8(local.y * 0.5f + 0.5f);
                    normalDepth[2] = Unorm8(local.z * 0.5f + 0.5f); normalDepth[3] = Unorm8(depth * 0.5f + 0.5f);
                }
        });
        DilateUncovered(atlas, 4);
        return atlas;
    }

    ImpostorImage LoadImpostorImage(const std::filesystem::path &path)
    {
        ImpostorImage image;
        int width = 0, height = 0, channels = 0;
        stbi_uc *pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (!pixels) return image;
        image.width = static_cast<std::uint32_t>(width);
        image.height = static_cast<std::uint32_t>(height);
        image.pixels.assign(pixels, pixels + std::size_t(width) * height * 4);
        stbi_image_free(pixels);
        return image;
    }

    ImpostorAtlas BakeImportedImpostor(const ImportedMeshSourceAsset &asset, const ImpostorBakeSettings &settings)
    {
        // Decode each base colour texture once, however many materials share it.
        std::unordered_map<int, ImpostorImage> images;
        const auto image = [&](int index) -> const ImpostorImage *
        {
            if (index < 0 || static_cast<std::size_t>(index) >= asset.textures.size()) return nullptr;
            auto [found, inserted] = images.try_emplace(index);
            if (inserted)
            {
                const auto &texture = asset.textures[static_cast<std::size_t>(index)];
                if (texture.width > 0 && texture.height > 0 &&
                    texture.pixels.size() >= std::size_t(texture.width) * texture.height * std::max(texture.channels, 1))
                {
                    auto &decoded = found->second;
                    decoded.width = static_cast<std::uint32_t>(texture.width);
                    decoded.height = static_cast<std::uint32_t>(texture.height);
                    decoded.pixels.resize(std::size_t(texture.width) * texture.height * 4);
                    const int channels = std::clamp(texture.channels, 1, 4);
                    for (std::size_t pixel = 0; pixel < std::size_t(texture.width) * texture.height; ++pixel)
                        for (int channel = 0; channel < 4; ++channel)
                            decoded.pixels[pixel * 4 + channel] = channel < channels
                                ? texture.pixels[pixel * channels + channel]
                                : channel == 3 ? 255 : texture.pixels[pixel * channels];
                }
                else if (!texture.sourcePath.empty())
                    found->second = LoadImpostorImage(texture.sourcePath);
            }
            return found->second.Empty() ? nullptr : &found->second;
        };
        std::vector<ImpostorMaterial> materials;
        materials.reserve(asset.materials.size());
        for (const auto &material : asset.materials)
            materials.push_back({material.color, image(material.albedoTextureIndex), material.alphaMode == render::AlphaMode::Mask,
                                 material.alphaCutoff, material.twoSided});
        return BakeImpostor(asset.meshData, asset.submeshes, materials, settings);
    }
}
