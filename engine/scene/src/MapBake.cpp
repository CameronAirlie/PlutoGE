#include "PlutoGE/scene/MapBake.h"

#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/Json.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>

namespace PlutoGE::scene
{
    namespace
    {
        using Polygon = std::vector<glm::vec2>;

        float Cross(const glm::vec2 &o, const glm::vec2 &a, const glm::vec2 &b)
        {
            return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
        }

        // Counter-clockwise convex hull (monotone chain); duplicates and collinear points dropped.
        Polygon Hull(std::vector<glm::vec2> points)
        {
            std::sort(points.begin(), points.end(), [](const auto &a, const auto &b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            points.erase(std::unique(points.begin(), points.end(), [](const auto &a, const auto &b) {
                             return std::abs(a.x - b.x) < 1e-4f && std::abs(a.y - b.y) < 1e-4f;
                         }), points.end());
            if (points.size() < 3) return points;
            Polygon hull(points.size() * 2);
            std::size_t k = 0;
            for (const auto &p : points)
            {
                while (k >= 2 && Cross(hull[k - 2], hull[k - 1], p) <= 0) --k;
                hull[k++] = p;
            }
            for (std::size_t i = points.size() - 1, lower = k + 1; i-- > 0;)
            {
                while (k >= lower && Cross(hull[k - 2], hull[k - 1], points[i]) <= 0) --k;
                hull[k++] = points[i];
            }
            hull.resize(k - 1);
            return hull;
        }

        struct Footprint
        {
            Polygon polygon;
            bool floor = false;
            glm::vec2 extent{0.0f};
        };

        bool Excluded(const Entity &entity, const std::vector<std::string> &tags)
        {
            for (const auto &tag : tags)
                if (entity.HasTag(tag)) return true;
            return false;
        }

        void Collect(const Entity *entity, const MapBakeSettings &settings, std::vector<Footprint> &footprints,
                     const NavigationMeshComponent *&navigation)
        {
            if (!entity || !entity->IsActiveInHierarchy() || Excluded(*entity, settings.excludeTags)) return;
            if (!navigation)
                if (const auto *nav = entity->GetComponent<NavigationMeshComponent>(); nav && nav->IsEnabled()) navigation = nav;
            if (const auto *collider = entity->GetComponent<ColliderComponent>();
                collider && collider->IsEnabled() && !collider->IsTrigger() && collider->GetShape() == ColliderShape::Box)
            {
                const glm::mat4 world = entity->GetWorldTransform();
                const glm::vec3 half = collider->GetSize() * 0.5f;
                std::vector<glm::vec2> corners;
                float low = std::numeric_limits<float>::max(), high = std::numeric_limits<float>::lowest();
                for (int i = 0; i < 8; ++i)
                {
                    const glm::vec3 local = collider->GetCenter() + half * glm::vec3(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
                    const glm::vec3 p = glm::vec3(world * glm::vec4(local, 1.0f));
                    low = std::min(low, p.y);
                    high = std::max(high, p.y);
                    corners.emplace_back(p.x, p.z);
                }
                Footprint footprint;
                footprint.polygon = Hull(std::move(corners));
                if (footprint.polygon.size() >= 3)
                {
                    glm::vec2 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
                    for (const auto &p : footprint.polygon) { minimum = glm::min(minimum, p); maximum = glm::max(maximum, p); }
                    footprint.extent = maximum - minimum;
                    const float height = high - low;
                    if (std::max(footprint.extent.x, footprint.extent.y) >= 0.05f)
                    {
                        footprint.floor = height <= settings.floorThickness ||
                                          height < settings.floorRatio * std::min(footprint.extent.x, footprint.extent.y);
                        footprints.push_back(std::move(footprint));
                    }
                }
            }
            for (const auto *child : entity->GetChildren()) Collect(child, settings, footprints, navigation);
        }

        // Fills a convex counter-clockwise polygon (pixel coordinates) into mask at pixel centres.
        void Fill(std::vector<std::uint8_t> &mask, int canvas, const Polygon &polygon)
        {
            glm::vec2 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
            for (const auto &p : polygon) { minimum = glm::min(minimum, p); maximum = glm::max(maximum, p); }
            const int x0 = std::max(0, static_cast<int>(std::floor(minimum.x))), x1 = std::min(canvas, static_cast<int>(std::ceil(maximum.x)));
            const int y0 = std::max(0, static_cast<int>(std::floor(minimum.y))), y1 = std::min(canvas, static_cast<int>(std::ceil(maximum.y)));
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                {
                    const glm::vec2 p(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                    bool inside = true;
                    for (std::size_t i = 0; i < polygon.size() && inside; ++i)
                        inside = Cross(polygon[i], polygon[(i + 1) % polygon.size()], p) >= 0;
                    if (inside) mask[static_cast<std::size_t>(y) * canvas + x] = 255;
                }
        }

        // Square max (dilate) or min (erode) filter of the given radius, separable.
        std::vector<std::uint8_t> Morph(const std::vector<std::uint8_t> &mask, int canvas, int radius, bool dilate)
        {
            std::vector<std::uint8_t> pass(mask.size()), out(mask.size());
            const auto pick = [dilate](std::uint8_t a, std::uint8_t b) { return dilate ? std::max(a, b) : std::min(a, b); };
            for (int y = 0; y < canvas; ++y)
                for (int x = 0; x < canvas; ++x)
                {
                    std::uint8_t value = dilate ? 0 : 255;
                    for (int d = -radius; d <= radius; ++d)
                    {
                        const int sx = std::clamp(x + d, 0, canvas - 1);
                        value = pick(value, mask[static_cast<std::size_t>(y) * canvas + sx]);
                    }
                    pass[static_cast<std::size_t>(y) * canvas + x] = value;
                }
            for (int y = 0; y < canvas; ++y)
                for (int x = 0; x < canvas; ++x)
                {
                    std::uint8_t value = dilate ? 0 : 255;
                    for (int d = -radius; d <= radius; ++d)
                    {
                        const int sy = std::clamp(y + d, 0, canvas - 1);
                        value = pick(value, pass[static_cast<std::size_t>(sy) * canvas + x]);
                    }
                    out[static_cast<std::size_t>(y) * canvas + x] = value;
                }
            return out;
        }
    }

    MapBakeResult BakeTopDownMap(const Scene &scene, const MapBakeSettings &settings)
    {
        MapBakeResult result;
        const int size = std::clamp(settings.size, 64, 4096);
        const int supersample = std::clamp(settings.supersample, 1, 4);
        std::vector<Footprint> footprints;
        const NavigationMeshComponent *navigation = nullptr;
        for (const auto *root : scene.GetRootEntities()) Collect(root, settings, footprints, navigation);

        glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
        for (const auto &footprint : footprints)
        {
            if (footprint.floor) continue;
            ++result.obstacleCount;
            for (const auto &p : footprint.polygon) { low = glm::min(low, p); high = glm::max(high, p); }
        }
        if (result.obstacleCount == 0)
        {
            result.message = "The scene has no solid box colliders to draw.";
            return result;
        }
        if (navigation && settings.cropToNavigation)
        {
            const auto &bounds = navigation->GetSettings();
            low = glm::max(low, glm::vec2(bounds.boundsMin.x, bounds.boundsMin.z));
            high = glm::min(high, glm::vec2(bounds.boundsMax.x, bounds.boundsMax.z));
            if (high.x <= low.x || high.y <= low.y)
            {
                result.message = "Navigation bounds do not overlap any solid collider.";
                return result;
            }
        }
        const glm::vec2 centre = (low + high) * 0.5f;
        const float span = std::max(high.x - low.x, high.y - low.y) + std::max(0.0f, settings.padding) * 2.0f;
        const int canvas = size * supersample;
        const auto toPixel = [&](const glm::vec2 &p) {
            return glm::vec2(((p.x - centre.x) / span + 0.5f) * canvas, ((p.y - centre.y) / span + 0.5f) * canvas);
        };

        std::vector<std::uint8_t> floorMask(static_cast<std::size_t>(canvas) * canvas), solidMask(floorMask.size());
        for (const auto &footprint : footprints)
        {
            // A ground plane spanning the frame adds nothing; the UI panel already provides it.
            if (footprint.floor && std::min(footprint.extent.x, footprint.extent.y) > span * 0.9f) continue;
            if (footprint.floor) ++result.floorCount;
            Polygon pixels;
            for (const auto &p : footprint.polygon) pixels.push_back(toPixel(p));
            Fill(footprint.floor ? floorMask : solidMask, canvas, pixels);
        }
        const int outlineRadius = std::max(1, static_cast<int>(std::lround(settings.style.outlineWidth * supersample * 0.5f)));
        const auto dilated = Morph(solidMask, canvas, outlineRadius, true);
        const auto eroded = Morph(solidMask, canvas, 1, false);

        // Compose premultiplied at full resolution, then box-filter to the output size.
        std::vector<glm::vec4> layer(static_cast<std::size_t>(canvas) * canvas, glm::vec4(0.0f));
        const auto over = [](glm::vec4 &dst, const glm::u8vec4 &colour, float coverage) {
            const float a = coverage * colour.a / 255.0f;
            dst = glm::vec4(glm::vec3(colour) / 255.0f * a, a) + dst * (1.0f - a);
        };
        for (std::size_t i = 0; i < layer.size(); ++i)
        {
            const float solid = solidMask[i] / 255.0f;
            over(layer[i], settings.style.floor, floorMask[i] / 255.0f * (1.0f - solid));
            over(layer[i], settings.style.obstacle, solid);
            over(layer[i], settings.style.outline, std::max(0, static_cast<int>(dilated[i]) - static_cast<int>(eroded[i])) / 255.0f);
        }
        result.rgba.resize(static_cast<std::size_t>(size) * size * 4);
        const float samples = static_cast<float>(supersample * supersample);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                glm::vec4 sum(0.0f);
                for (int sy = 0; sy < supersample; ++sy)
                    for (int sx = 0; sx < supersample; ++sx)
                        sum += layer[static_cast<std::size_t>(y * supersample + sy) * canvas + x * supersample + sx];
                sum /= samples;
                const glm::vec3 straight = sum.a > 1e-6f ? glm::vec3(sum) / sum.a : glm::vec3(0.0f);
                auto *pixel = &result.rgba[(static_cast<std::size_t>(y) * size + x) * 4];
                for (int c = 0; c < 3; ++c) pixel[c] = static_cast<std::uint8_t>(std::lround(std::clamp(straight[c], 0.0f, 1.0f) * 255.0f));
                pixel[3] = static_cast<std::uint8_t>(std::lround(std::clamp(sum.a, 0.0f, 1.0f) * 255.0f));
            }
        result.success = true;
        result.size = size;
        result.centreX = centre.x;
        result.centreZ = centre.y;
        result.worldSize = span;
        result.message = std::to_string(result.obstacleCount) + " obstacles, " + std::to_string(result.floorCount) +
                         " floors, " + std::to_string(static_cast<int>(std::lround(span))) + " m square";
        return result;
    }

    bool WriteTga(const std::filesystem::path &path, int width, int height, const std::vector<std::uint8_t> &rgba, std::string *error)
    {
        if (width <= 0 || height <= 0 || width > 65535 || height > 65535 || rgba.size() != static_cast<std::size_t>(width) * height * 4)
        {
            if (error) *error = "Invalid TGA image dimensions.";
            return false;
        }
        std::array<std::uint8_t, 18> header{};
        header[2] = 2; // Uncompressed true colour.
        header[12] = static_cast<std::uint8_t>(width & 0xFF);
        header[13] = static_cast<std::uint8_t>(width >> 8);
        header[14] = static_cast<std::uint8_t>(height & 0xFF);
        header[15] = static_cast<std::uint8_t>(height >> 8);
        header[16] = 32;
        header[17] = 0x28; // 8 alpha bits, top-left origin.
        std::vector<std::uint8_t> bgra(rgba.size());
        for (std::size_t i = 0; i < rgba.size(); i += 4)
        {
            bgra[i] = rgba[i + 2]; bgra[i + 1] = rgba[i + 1]; bgra[i + 2] = rgba[i]; bgra[i + 3] = rgba[i + 3];
        }
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(header.data()), header.size());
        file.write(reinterpret_cast<const char *>(bgra.data()), static_cast<std::streamsize>(bgra.size()));
        if (!file)
        {
            if (error) *error = "Could not write " + path.string();
            return false;
        }
        return true;
    }

    std::string MakeMapLayoutJson(const std::string &name, const std::string &imageReference, float centreX, float centreZ, float worldSize)
    {
        auto layout = assets::JsonValue::MakeObject();
        layout.Set("Name", assets::JsonValue::MakeString(name));
        layout.Set("Image", assets::JsonValue::MakeString(imageReference));
        // Rounded to millimetres so rebakes of unchanged geometry produce identical files.
        const auto rounded = [](float value) { return std::round(static_cast<double>(value) * 1000.0) / 1000.0; };
        layout.Set("CentreX", assets::JsonValue::MakeNumber(rounded(centreX)));
        layout.Set("CentreZ", assets::JsonValue::MakeNumber(rounded(centreZ)));
        layout.Set("WorldSize", assets::JsonValue::MakeNumber(rounded(worldSize)));
        layout.Set("North", assets::JsonValue::MakeString("-Z"));
        return assets::WriteJson(layout);
    }

    bool SaveMapBake(assets::Project &project, const MapBakeResult &result, const std::string &name, const std::string &folder,
                     MapBakeOutput *output, std::string *error)
    {
        if (!result.success)
        {
            if (error) *error = result.message.empty() ? "The map bake failed." : result.message;
            return false;
        }
        if (name.empty() || name.find_first_of("/\\:") != std::string::npos)
        {
            if (error) *error = "Invalid map name.";
            return false;
        }
        const auto directory = project.GetAssetDirectoryPath() / std::filesystem::path(folder).relative_path();
        MapBakeOutput written;
        written.image = directory / (name + ".tga");
        written.layout = directory / (name + ".plutomap");
        if (!WriteTga(written.image, result.size, result.size, result.rgba, error)) return false;
        written.imageReference = project.MakeAssetReference(written.image);
        written.layoutReference = project.MakeAssetReference(written.layout);
        {
            std::ofstream file(written.layout, std::ios::binary | std::ios::trunc);
            file << MakeMapLayoutJson(name, written.imageReference, result.centreX, result.centreZ, result.worldSize);
            if (!file)
            {
                if (error) *error = "Could not write " + written.layout.string();
                return false;
            }
        }
        // Give new files a stable identity sidecar; rebakes keep the existing ID.
        for (const auto &path : {written.image, written.layout})
        {
            assets::AssetMetadata metadata;
            const auto metadataPath = assets::GetAssetMetadataPath(path);
            const auto status = assets::LoadAssetMetadata(metadataPath, metadata);
            if (status == assets::AssetMetadataStatus::Missing)
            {
                metadata = {};
                metadata.id = assets::GenerateAssetId();
                if (!assets::SaveAssetMetadata(metadataPath, metadata, assets::AssetMetadataWriteMode::CreateOnly, error)) return false;
            }
            else if (status != assets::AssetMetadataStatus::Success)
            {
                if (error) *error = "Unreadable asset metadata " + metadataPath.string();
                return false;
            }
        }
        if (!project.RefreshAssetEntry(written.image, error) || !project.RefreshAssetEntry(written.layout, error)) return false;
        auto &include = project.GetManifest().buildAlwaysInclude;
        for (const auto &reference : {written.imageReference, written.layoutReference})
            if (std::find(include.begin(), include.end(), reference) == include.end()) include.push_back(reference);
        if (!project.Save(error)) return false;
        if (output) *output = std::move(written);
        return true;
    }
}
