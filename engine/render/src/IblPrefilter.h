#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    inline glm::vec3 IblFaceDirection(unsigned face, float u, float v)
    {
        const glm::vec3 directions[] = {{1,-v,-u},{-1,-v,u},{u,1,v},{u,-1,-v},{u,-v,1},{-u,-v,-1}};
        return glm::normalize(directions[face]);
    }

    inline glm::vec3 SampleIblFaces(std::span<const float> pixels, unsigned size, glm::vec3 d)
    {
        const auto a = glm::abs(d);
        unsigned face;
        glm::vec2 uv;
        if (a.x >= a.y && a.x >= a.z) {
            face = d.x >= 0 ? 0 : 1;
            uv = glm::vec2(d.x >= 0 ? -d.z : d.z, -d.y) / a.x;
        } else if (a.y >= a.z) {
            face = d.y >= 0 ? 2 : 3;
            uv = glm::vec2(d.x, d.y >= 0 ? d.z : -d.z) / a.y;
        } else {
            face = d.z >= 0 ? 4 : 5;
            uv = glm::vec2(d.z >= 0 ? d.x : -d.x, -d.y) / a.z;
        }
        uv = (uv * .5f + .5f) * float(size) - .5f;
        const int x = int(std::floor(uv.x)), y = int(std::floor(uv.y));
        const auto at = [&](int px, int py) {
            const auto offset = ((std::size_t(face) * size + std::clamp(py, 0, int(size)-1)) * size + std::clamp(px, 0, int(size)-1)) * 4;
            return glm::vec3(pixels[offset], pixels[offset+1], pixels[offset+2]);
        };
        return glm::mix(glm::mix(at(x,y), at(x+1,y), uv.x-x),
                        glm::mix(at(x,y+1), at(x+1,y+1), uv.x-x), uv.y-y);
    }

    // Pack separately convolved roughness levels beside the full-resolution
    // faces. Hardware image downsampling cannot substitute for GGX filtering.
    // Level l has size R/2^l and starts at x=2R-2*size, y=0.
    inline std::vector<float> BuildIblPrefilterAtlas(std::span<const float> faces, unsigned resolution)
    {
        const unsigned width = resolution * 2;
        std::vector<float> atlas(std::size_t(width) * resolution * 6 * 4);
        const unsigned maxLevel = unsigned(std::log2(resolution));
        constexpr unsigned samples = 128;
        constexpr float pi = 3.14159265359f;
        for (unsigned level = 0; level <= maxLevel; ++level)
        {
            const unsigned size = resolution >> level;
            const unsigned start = width - size * 2;
            std::vector<glm::vec3> halfVectors;
            if (level > 0) {
                const float roughness = float(level) / maxLevel;
                const float alpha = roughness * roughness;
                for (unsigned i = 0; i < samples; ++i) {
                    unsigned bits = i;
                    bits = (bits << 16u) | (bits >> 16u);
                    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
                    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
                    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
                    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
                    const float xi = float(bits) * 2.3283064365386963e-10f;
                    const float phi = 2 * pi * float(i) / samples;
                    const float cosine = std::sqrt((1-xi) / (1+(alpha*alpha-1)*xi));
                    const float sine = std::sqrt(std::max(0.f, 1-cosine*cosine));
                    halfVectors.emplace_back(std::cos(phi)*sine, std::sin(phi)*sine, cosine);
                }
            }
            for (unsigned face = 0; face < 6; ++face)
                for (unsigned y = 0; y < size; ++y)
                    for (unsigned x = 0; x < size; ++x)
                    {
                        const auto n = IblFaceDirection(face, (x+.5f)*2/size-1, (y+.5f)*2/size-1);
                        glm::vec3 color(0);
                        if (level == 0) color = SampleIblFaces(faces, resolution, n);
                        else {
                            const auto up = std::abs(n.z) < .999f ? glm::vec3(0,0,1) : glm::vec3(1,0,0);
                            const auto tangent = glm::normalize(glm::cross(up,n));
                            const auto bitangent = glm::cross(n,tangent);
                            float weight = 0;
                            for (const auto hLocal : halfVectors) {
                                const auto h = tangent*hLocal.x + bitangent*hLocal.y + n*hLocal.z;
                                const auto l = 2*glm::dot(n,h)*h-n;
                                const float ndotl = glm::dot(n,l);
                                if (ndotl > 0) {
                                    color += SampleIblFaces(faces, resolution, l) * ndotl;
                                    weight += ndotl;
                                }
                            }
                            color /= std::max(weight, .00001f);
                        }
                        const auto offset = ((std::size_t(face)*size+y)*width+start+x)*4;
                        atlas[offset]=color.r; atlas[offset+1]=color.g; atlas[offset+2]=color.b; atlas[offset+3]=1;
                    }
        }
        return atlas;
    }
}
