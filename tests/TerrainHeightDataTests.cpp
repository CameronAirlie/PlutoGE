// Terrain heights without a GPU: the .plutoheight format, heightmap import from other tools (8/16-bit images, raw R16 and
// R32F), resampling, the border ring that lets neighbouring tiles shade seamlessly, and the terrain's height file,
// sculpting regions and persistence.
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/TerrainHeightData.h"
#include "PlutoGE/scene/components/TerrainComponent.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1e-5f) { return std::abs(a - b) <= tolerance; }

    std::filesystem::path TempDirectory()
    {
        const auto path = std::filesystem::temp_directory_path() / "PlutoGETerrainHeightDataTests";
        std::filesystem::create_directories(path);
        return path;
    }

    void WriteFile(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    // ---- a minimal greyscale PNG writer (stored deflate blocks), so the tests do not depend on an encoder ----------
    std::uint32_t Crc(const std::uint8_t *data, std::size_t size, std::uint32_t crc = 0xffffffffu)
    {
        for (std::size_t i = 0; i < size; ++i)
        {
            crc ^= data[i];
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
        return crc;
    }

    void PutBigEndian(std::vector<std::uint8_t> &out, std::uint32_t value)
    {
        for (int shift = 24; shift >= 0; shift -= 8)
            out.push_back(static_cast<std::uint8_t>(value >> shift));
    }

    void Chunk(std::vector<std::uint8_t> &png, const char type[4], const std::vector<std::uint8_t> &data)
    {
        PutBigEndian(png, static_cast<std::uint32_t>(data.size()));
        std::vector<std::uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        png.insert(png.end(), body.begin(), body.end());
        PutBigEndian(png, Crc(body.data(), body.size()) ^ 0xffffffffu);
    }

    // `rows` lists the image's rows top first; 8 or 16 bits per sample.
    std::vector<std::uint8_t> GreyPng(int width, int height, int bits, const std::vector<std::uint16_t> &rows)
    {
        std::vector<std::uint8_t> raw;
        for (int y = 0; y < height; ++y)
        {
            raw.push_back(0); // no filter
            for (int x = 0; x < width; ++x)
            {
                const auto value = rows[static_cast<std::size_t>(y) * width + x];
                if (bits == 16)
                    raw.push_back(static_cast<std::uint8_t>(value >> 8));
                raw.push_back(static_cast<std::uint8_t>(value));
            }
        }
        std::vector<std::uint8_t> zlib{0x78, 0x01};
        for (std::size_t offset = 0; offset < raw.size(); offset += 65535)
        {
            const auto length = static_cast<std::uint16_t>(std::min<std::size_t>(65535, raw.size() - offset));
            zlib.push_back(offset + length >= raw.size() ? 1 : 0);
            zlib.push_back(static_cast<std::uint8_t>(length));
            zlib.push_back(static_cast<std::uint8_t>(length >> 8));
            zlib.push_back(static_cast<std::uint8_t>(~length));
            zlib.push_back(static_cast<std::uint8_t>(~length >> 8));
            zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset), raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        }
        std::uint32_t a = 1, b = 0;
        for (const auto byte : raw)
        {
            a = (a + byte) % 65521u;
            b = (b + a) % 65521u;
        }
        PutBigEndian(zlib, (b << 16) | a);

        std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        std::vector<std::uint8_t> header;
        PutBigEndian(header, static_cast<std::uint32_t>(width));
        PutBigEndian(header, static_cast<std::uint32_t>(height));
        header.insert(header.end(), {static_cast<std::uint8_t>(bits), 0, 0, 0, 0});
        Chunk(png, "IHDR", header);
        Chunk(png, "IDAT", zlib);
        Chunk(png, "IEND", {});
        return png;
    }

    TerrainHeightField Field(int width, int depth, float (*height)(int, int))
    {
        TerrainHeightField field{width, depth, {}};
        field.samples.resize(static_cast<std::size_t>(width) * depth);
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
                field.At(x, z) = height(x, z);
        return field;
    }

    float Bumps(int x, int z) { return 5.0f + 2.0f * std::sin(0.7f * x) + 1.5f * std::cos(0.45f * z) + 0.05f * x * z; }

    // A window of a larger field with its border ring taken from the field (what tile splitting produces).
    TerrainHeightData Window(const TerrainHeightField &source, int x0, int z0, int width, int depth)
    {
        TerrainHeightData data;
        data.field = {width, depth, std::vector<float>(static_cast<std::size_t>(width) * depth)};
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
                data.field.At(x, z) = source.At(x0 + x, z0 + z);
        data.border.resize(TerrainHeightBorder::Size(width, depth));
        for (int z = -TerrainHeightBorder::Width; z < depth + TerrainHeightBorder::Width; ++z)
            for (int x = -TerrainHeightBorder::Width; x < width + TerrainHeightBorder::Width; ++x)
                if (const auto index = TerrainHeightBorder::Index(width, depth, x, z); index != TerrainHeightBorder::npos)
                    data.border[index] = source.At(std::clamp(x0 + x, 0, source.width - 1), std::clamp(z0 + z, 0, source.depth - 1));
        return data;
    }

    void FileFormat()
    {
        TerrainHeightData data;
        data.field = Field(5, 4, Bumps);
        data.border = TerrainHeightBorder::Clamped(data.field);
        data.border[3] = 42.0f;

        const auto exact = TerrainHeightFile::Encode(data);
        Require(exact.size() == TerrainHeightFile::HeaderSize + (20 + data.border.size()) * 4, "Float32 file size");
        const auto decoded = TerrainHeightFile::Decode(exact);
        Require(decoded && *decoded == data, "Float32 files must round-trip exactly");

        const auto compact = TerrainHeightFile::Encode(data, TerrainHeightEncoding::Unorm16);
        Require(compact.size() == TerrainHeightFile::HeaderSize + (20 + data.border.size()) * 2, "Unorm16 file size");
        const auto quantized = TerrainHeightFile::Decode(compact);
        Require(quantized && quantized->field.width == 5 && quantized->field.depth == 4 && quantized->border.size() == data.border.size(),
                "Unorm16 files keep their grid and border");
        const float step = (42.0f - 0.0f) / 65535.0f;
        for (std::size_t i = 0; i < data.field.samples.size(); ++i)
            Require(Near(quantized->field.samples[i], data.field.samples[i], step), "Unorm16 samples stay within one step");
        Require(Near(quantized->border[3], 42.0f, step), "Unorm16 keeps the range's top");

        TerrainHeightData plain;
        plain.field = Field(3, 3, Bumps);
        const auto noBorder = TerrainHeightFile::Decode(TerrainHeightFile::Encode(plain));
        Require(noBorder && noBorder->border.empty() && *noBorder == plain, "A file without a border has none");

        std::string error;
        auto truncated = exact;
        truncated.resize(truncated.size() - 1);
        Require(!TerrainHeightFile::Decode(truncated, &error) && !error.empty(), "A truncated file is refused");
        auto wrongMagic = exact;
        wrongMagic[0] = 'X';
        Require(!TerrainHeightFile::Decode(wrongMagic), "Another file type is refused");
        auto huge = exact;
        huge[8] = 0xff, huge[9] = 0xff, huge[10] = 0xff, huge[11] = 0x7f;
        Require(!TerrainHeightFile::Decode(huge, &error), "An absurd grid size is refused before allocating");
        auto future = exact;
        future[4] = 9;
        Require(!TerrainHeightFile::Decode(future, &error) && error.find("version") != std::string::npos, "Future versions are refused");
        Require(TerrainHeightFile::Encode(TerrainHeightData{}).empty(), "Nothing to encode gives no bytes");
    }

    void BorderLayout()
    {
        const int width = 4, depth = 3;
        std::vector<int> hits(TerrainHeightBorder::Size(width, depth), 0);
        for (int z = -3; z <= depth + 2; ++z)
            for (int x = -3; x <= width + 2; ++x)
            {
                const auto index = TerrainHeightBorder::Index(width, depth, x, z);
                const bool inRing = x >= -2 && x < width + 2 && z >= -2 && z < depth + 2 && (x < 0 || z < 0 || x >= width || z >= depth);
                Require((index != TerrainHeightBorder::npos) == inRing, "Ring membership is wrong at " + std::to_string(x) + "," + std::to_string(z));
                if (index != TerrainHeightBorder::npos)
                    ++hits.at(index);
            }
        for (const int count : hits)
            Require(count == 1, "Every ring slot belongs to exactly one sample");

        const auto field = Field(width, depth, Bumps);
        const auto ring = TerrainHeightBorder::Clamped(field);
        Require(ring[TerrainHeightBorder::Index(width, depth, -2, -2)] == field.At(0, 0) &&
                    ring[TerrainHeightBorder::Index(width, depth, width + 1, 1)] == field.At(width - 1, 1),
                "A clamped ring repeats the edges");
    }

    void ImportFormats()
    {
        // Raw 16-bit, top row first: rows (far edge first) 0,1 / 2,3 -> near row is 2,3.
        const std::vector<std::uint8_t> little{0, 0, 0xff, 0xff, 0x00, 0x80, 0x01, 0x00};
        auto raw = DecodeTerrainHeightSource(little, "terrain.r16");
        Require(raw && raw->normalized && raw->bitsPerSample == 16 && raw->field.width == 2 && raw->field.depth == 2,
                "A square raw R16 file is read from its size");
        Require(Near(raw->field.At(0, 0), 32768.0f / 65535.0f) && Near(raw->field.At(1, 0), 1.0f / 65535.0f) &&
                    Near(raw->field.At(0, 1), 0.0f) && Near(raw->field.At(1, 1), 1.0f),
                "Raw rows are flipped so the top row is the far edge");

        TerrainHeightImportOptions bigEndian;
        bigEndian.rawBigEndian = true;
        bigEndian.flipRows = false;
        raw = DecodeTerrainHeightSource(little, "terrain.raw", bigEndian);
        Require(raw && Near(raw->field.At(0, 1), 128.0f / 65535.0f) && Near(raw->field.At(1, 1), 256.0f / 65535.0f),
                "Big-endian raw samples, unflipped");

        std::string error;
        TerrainHeightImportOptions sized;
        sized.rawWidth = 3;
        Require(!DecodeTerrainHeightSource(little, "terrain.r16", sized, &error) && !error.empty(), "A size that does not fit is refused");
        Require(!DecodeTerrainHeightSource(std::vector<std::uint8_t>{1, 2, 3}, "odd.r16", {}, &error), "Odd raw sizes are refused");

        // Raw float, 3 x 2 given explicitly: values kept as stored.
        std::vector<std::uint8_t> floats;
        for (const float value : {1.0f, 2.0f, 3.0f, 10.0f, 20.0f, 30.0f})
        {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (int shift = 0; shift < 32; shift += 8)
                floats.push_back(static_cast<std::uint8_t>(bits >> shift));
        }
        TerrainHeightImportOptions floatOptions;
        floatOptions.rawWidth = 3;
        floatOptions.rawDepth = 2;
        const auto floatField = DecodeTerrainHeightSource(floats, "terrain.r32", floatOptions);
        Require(floatField && !floatField->normalized && floatField->field.At(0, 0) == 10.0f && floatField->field.At(2, 1) == 3.0f,
                "Raw float samples keep their values");

        // A 16-bit PNG keeps 16-bit steps that an 8-bit read would merge.
        const auto png16 = GreyPng(2, 2, 16, {1, 2, 65535, 0});
        const auto deep = DecodeTerrainHeightSource(png16, "height.png");
        Require(deep && deep->bitsPerSample == 16 && deep->normalized, "A 16-bit PNG is read at 16 bits");
        Require(Near(deep->field.At(0, 1), 1.0f / 65535.0f, 1e-7f) && Near(deep->field.At(1, 1), 2.0f / 65535.0f, 1e-7f) &&
                    deep->field.At(0, 1) != deep->field.At(1, 1) && Near(deep->field.At(0, 0), 1.0f),
                "16-bit precision was lost");

        const auto png8 = GreyPng(3, 2, 8, {0, 128, 255, 10, 20, 30});
        const auto shallow = DecodeTerrainHeightSource(png8, "height.png");
        Require(shallow && shallow->bitsPerSample == 8 && Near(shallow->field.At(2, 1), 1.0f) && Near(shallow->field.At(1, 0), 20.0f / 255.0f),
                "An 8-bit PNG is read and flipped");

        TerrainHeightData stored;
        stored.field = Field(3, 3, Bumps);
        const auto fromFile = DecodeTerrainHeightSource(TerrainHeightFile::Encode(stored), "anything.bin");
        Require(fromFile && !fromFile->normalized && fromFile->field == stored.field, ".plutoheight is recognised by its content");
        Require(TerrainHeightFormatFromPath("C:/maps/World.R16") == TerrainHeightSourceFormat::Raw16 &&
                    TerrainHeightFormatFromPath("a.plutoheight") == TerrainHeightSourceFormat::PlutoHeight &&
                    TerrainHeightFormatFromPath("dir.v2/height") == TerrainHeightSourceFormat::Auto,
                "Formats from paths");
    }

    void Resampling()
    {
        const auto field = Field(5, 5, [](int x, int z) { return static_cast<float>(x + 10 * z); });
        const auto up = ResampleHeightField(field, 9, 9);
        Require(up.Valid() && up.At(0, 0) == 0.0f && up.At(8, 8) == 44.0f && Near(up.At(1, 0), 0.5f) && Near(up.At(0, 1), 5.0f),
                "Upsampling keeps the corners and interpolates between");
        const auto down = ResampleHeightField(field, 3, 3);
        Require(down.At(1, 1) == field.At(2, 2) && down.At(2, 0) == field.At(4, 0), "Downsampling a linear ramp is exact");
        Require(!ResampleHeightField(field, 1, 5).Valid(), "A one-sample resample is refused");
    }

    void ComponentHeightFile()
    {
        const auto directory = TempDirectory();
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
        auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 9, .depth = 9, .cellSize = 2.0f, .heightScale = 100.0f});

        // Import: a normalized source maps onto the range and the grid takes its size.
        const auto pngPath = directory / "import16.png";
        WriteFile(pngPath, GreyPng(3, 2, 16, {0, 32768, 65535, 65535, 0, 0}));
        std::string error;
        Require(terrain->ImportHeightMap(pngPath.string(), {}, glm::vec2(10.0f, 50.0f), &error), "Import failed: " + error);
        // The image's bottom row (65535, 0, 0) is the near edge; its top row (0, 32768, 65535) the far one.
        const auto &imported = terrain->GetHeightSamples();
        Require(terrain->GetWidth() == 3 && terrain->GetDepth() == 2 && Near(imported[0], 50.0f) && Near(imported[1], 10.0f) &&
                    Near(imported[3], 10.0f) && Near(imported[4], 10.0f + 40.0f * 32768.0f / 65535.0f, 1e-3f) && Near(imported[5], 50.0f),
                "Imported heights are wrong");
        Require(terrain->GetHoles().GetCellsX() == 2 && terrain->GetHoles().GetCellsZ() == 1, "Holes follow the new grid");
        Require(!terrain->ImportHeightMap((directory / "missing.png").string(), {}, std::nullopt, &error) && !error.empty(),
                "A missing file is an error");
        // LoadHeightMap reads 16-bit images at full precision now.
        Require(terrain->LoadHeightMap(pngPath.string()) && terrain->GetHeightMapPath() == pngPath.string() &&
                    Near(terrain->GetHeightSamples()[4], 100.0f * 32768.0f / 65535.0f, 1e-3f),
                "LoadHeightMap keeps 16 bits");

        // A height file: heights and border round-trip, and the scene stops carrying samples.
        TerrainHeightData data = Window(Field(20, 20, Bumps), 4, 4, 9, 9);
        terrain->SetHeightData(data);
        Require(terrain->GetWidth() == 9 && terrain->GetHeightBorder().size() == data.border.size(), "SetHeightData takes the grid and border");
        const auto heightFile = (directory / "terrain.plutoheight").string();
        terrain->SetHeightDataPath(heightFile);
        Require(terrain->SaveHeightData(TerrainHeightEncoding::Float32, &error), "Save failed: " + error);
        Require(!std::filesystem::exists(heightFile + ".writing"), "The staging file is cleaned up");

        bool hasSamples = false, hasPath = false;
        for (const auto &property : terrain->SerializeShared())
        {
            hasSamples |= property.name == "HeightSamples";
            hasPath |= property.name == "HeightData" && property.value == heightFile;
        }
        Require(!hasSamples && hasPath, "A terrain with a height file does not write its samples into the scene");

        Scene loaded;
        auto *copyEntity = loaded.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Copy"}));
        auto *copy = copyEntity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.heightScale = 100.0f});
        copy->Deserialize(terrain->Serialize());
        Require(copy->GetHeightData() == terrain->GetHeightData(), "A scene naming the file loads the heights and border from it");

        // The file is read only when the scene names a different one: unrelated undo steps keep newer sculpting.
        copy->SetPaintEnabled(true);
        copy->SetPaintMode(TerrainPaintMode::Raise);
        copy->SetBrushRadius(3.0f);
        copy->SetBrushStrength(20.0f);
        const auto rect = copy->SculptAtWorldPosition({8.0f, 0.0f, 8.0f}, 0.1f);
        Require(!rect.Empty() && rect.x0 <= 4 && rect.x1 > 4 && rect.z0 <= 4 && rect.z1 > 4, "Sculpting reports the samples it changed");
        const auto sculpted = copy->GetHeightSamples();
        copy->Deserialize(terrain->Serialize());
        Require(copy->GetHeightSamples() == sculpted, "Re-applying the same scene state must not reload the file");

        // Regions capture and restore exactly what changed.
        const auto after = copy->CaptureHeights(rect);
        copy->RestoreHeights(terrain->CaptureHeights(rect));
        Require(copy->GetHeightSamples() == terrain->GetHeightSamples(), "Restoring the before-region undoes the stroke");
        copy->RestoreHeights(after);
        Require(copy->GetHeightSamples() == sculpted, "Restoring the after-region redoes it");
        const auto clipped = copy->CaptureHeights({-5, -5, 2, 2});
        Require(clipped.rect == TerrainSampleRect{0, 0, 2, 2} && clipped.samples.size() == 4, "Regions clip to the grid");

        // Back to scene-held heights: samples from an older state apply and the border goes.
        Property samples{"HeightSamples", PropertyType::String, ""};
        for (int i = 0; i < 81; ++i)
            samples.value += (i ? "," : "") + std::string("7");
        copy->Deserialize({{"HeightData", PropertyType::String, ""}, samples});
        Require(copy->GetHeightDataPath().empty() && copy->GetHeightBorder().empty() && Near(copy->GetHeightSamples()[40], 7.0f),
                "Clearing the height file takes the scene's samples again");

        std::filesystem::remove(heightFile);
        Require(!copy->ImportHeightMap(heightFile), "Importing a removed file fails");
    }

    // Two tiles cut from one field, each with its border ring, smooth their surface identically along the shared edge
    // and one sample beyond it, which is everything their normals there read.
    void SeamlessTiles()
    {
        const auto world = Field(17, 9, Bumps);
        Scene scene;
        auto *left = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Left"}))->CreateComponent<TerrainComponent>(
            TerrainComponentConfig{.width = 9, .depth = 9, .heightScale = 100.0f});
        auto *right = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Right"}))->CreateComponent<TerrainComponent>(
            TerrainComponentConfig{.width = 9, .depth = 9, .heightScale = 100.0f});
        left->SetHeightData(Window(world, 0, 0, 9, 9));
        right->SetHeightData(Window(world, 8, 0, 9, 9));
        for (int z = 0; z < 9; ++z)
        {
            Require(Near(left->GetSurfaceSampleHeight(8, z), right->GetSurfaceSampleHeight(0, z)), "The shared edge differs");
            Require(Near(left->GetSurfaceSampleHeight(9, z), right->GetSurfaceSampleHeight(1, z)) &&
                        Near(left->GetSurfaceSampleHeight(7, z), right->GetSurfaceSampleHeight(-1, z)),
                    "The samples beside the edge differ, so normals would too");
        }
        // Without the border the edge clamps, and the tiles disagree: what the border is for.
        auto lone = Window(world, 0, 0, 9, 9);
        lone.border.clear();
        left->SetHeightData(lone);
        bool differs = false;
        for (int z = 0; z < 9; ++z)
            differs |= !Near(left->GetSurfaceSampleHeight(9, z), right->GetSurfaceSampleHeight(1, z));
        Require(differs, "A lone terrain should clamp at its edge");
    }

    void LodDistanceScale()
    {
        TerrainComponent terrain(TerrainComponentConfig{.width = 65, .depth = 65});
        const auto threshold = [&] { return terrain.GetMeshForBaking()->GetSubmesh(0).lods[1].maxScreenRadiusPixels; };
        const float base = threshold();
        terrain.SetLodDistanceScale(2.0f);
        Require(Near(threshold(), base / 2.0f, 1e-3f), "Doubling the LOD distance scale halves the switch size");
        terrain.Deserialize({{"LodDistanceScale", PropertyType::Float, "100"}});
        Require(terrain.GetLodDistanceScale() == 16.0f, "The scale is clamped");
    }
}

int main()
{
    try
    {
        FileFormat();
        BorderLayout();
        ImportFormats();
        Resampling();
        ComponentHeightFile();
        SeamlessTiles();
        LodDistanceScale();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Terrain height data tests passed.\n";
    return 0;
}
