#include "PlutoGE/render/DdsImage.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool value)
{
    if (!value)
        throw std::runtime_error("DDS regression failed");
}
int main(int argc, char **argv)
{
    using namespace PlutoGE::render;
    std::vector<unsigned char> bytes(144);
    auto put = [&](size_t p, uint32_t v) {
        for (int i = 0; i < 4; ++i)
            bytes[p + i] = static_cast<unsigned char>(v >> (8 * i));
    };
    put(0, 0x20534444);
    put(4, 124);
    put(12, 4);
    put(16, 4);
    put(76, 32);
    put(80, 4);
    put(84, 0x31545844);
    bytes[128] = 0;
    bytes[129] = 248; // red RGB565 endpoint
    DdsImage image;
    Check(DecodeDds(bytes, image));
    Check(image.width == 4 && image.height == 4);
    for (size_t i = 0; i < image.pixels.size(); i += 4)
        Check(image.pixels[i] == 255 && image.pixels[i + 1] == 0 && image.pixels[i + 3] == 255);
    bytes[128] = bytes[129] = 0;
    bytes[130] = 255;
    bytes[131] = 255;
    std::fill(bytes.begin() + 132, bytes.begin() + 136, 255);
    Check(DecodeDds(bytes, image));
    Check(image.pixels[3] == 0); // BC1 transparent selector
    put(84, 0x35545844);
    std::fill(bytes.begin() + 128, bytes.end(), 0);
    bytes[128] = 123;
    bytes[137] = 248;
    Check(DecodeDds(bytes, image));
    Check(image.pixels[0] == 255 && image.pixels[3] == 123);
    put(84, 0x32495441);
    bytes[128] = 128;
    bytes[136] = 128;
    Check(DecodeDds(bytes, image));
    Check(image.pixels[0] == 128 && image.pixels[1] == 128 && image.pixels[2] == 255 && image.pixels[3] == 255);
    put(12, 1);
    put(16, 3);
    Check(DecodeDds(bytes, image));
    Check(image.pixels.size() == 12);
    Check(!DecodeDds(std::span(bytes).first(143), image));
    Check(image.pixels.empty());
    put(112, 0x200);
    Check(!DecodeDds(bytes, image));
    put(112, 0);
    put(84, 0x30315844);
    bytes.resize(164);
    put(128, 83);
    put(132, 3);
    put(140, 1);
    bytes[148] = 128;
    bytes[156] = 128;
    Check(DecodeDds(bytes, image));
    Check(image.pixels[2] == 255);
    put(140, 2);
    Check(!DecodeDds(bytes, image));
    if (argc > 1)
    {
        size_t count = 0;
        for (const auto &entry : std::filesystem::directory_iterator(argv[1]))
        {
            if (entry.path().extension() != ".dds")
                continue;
            std::ifstream input(entry.path(), std::ios::binary);
            std::vector<unsigned char> data((std::istreambuf_iterator<char>(input)), {});
            Check(DecodeDds(data, image));
            ++count;
        }
        std::cout << "Decoded " << count << " DDS textures\n";
    }
}
