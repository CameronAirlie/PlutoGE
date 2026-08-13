#include "PlutoGE/render/passes/RuntimeUIPass.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kFirstBakedCodepoint = 32;
        constexpr int kBakedCodepointCount = 224;
        constexpr float kUIUnitsPerWorldUnit = 100.0f;

        struct UIRect
        {
            glm::vec2 min{0.0f};
            glm::vec2 max{0.0f};
        };

        UIRect IntersectRects(const UIRect &a, const UIRect &b)
        {
            return {.min = glm::max(a.min, b.min), .max = glm::min(a.max, b.max)};
        }

        struct UIQuad
        {
            UIRect rect;
            UIRect clipRect;
            glm::vec2 uvMin{0.0f};
            glm::vec2 uvMax{1.0f};
            glm::vec4 color{1.0f};
            GLuint textureId = 0;
            float depth = 0.0f;
            bool depthTest = false;
            int sortingOrder = 0;
            std::uint32_t entityId = 0;
            scene::UIImageType imageType = scene::UIImageType::Simple;
            float fillAmount = 1.0f;
            float thickness = 2.0f;
            float cornerRadius = 0.0f;
            float startAngle = 0.0f;
            float rotation = 0.0f;
            glm::vec2 localScale{1.0f};
            bool clipped = false;
        };

        struct UIQuadInstance
        {
            glm::vec4 rect;
            glm::vec4 uv;
            glm::vec4 color;
            glm::vec4 shape;
            glm::vec4 transform;
            glm::vec2 depth;
        };

        struct UITextRun
        {
            UIRect rect;
            UIRect clipRect;
            std::string text;
            std::string fontPath;
            glm::vec4 color{1.0f};
            float fontSize = 18.0f;
            float pixelScale = 1.0f;
            float depth = 0.0f;
            bool depthTest = false;
            bool richText = true;
            int sortingOrder = 0;
            std::uint32_t entityId = 0;
            scene::UITextAlignment alignment = scene::UITextAlignment::MiddleCenter;
            bool wrap = true;
            float lineSpacing = 1.0f;
            glm::vec4 outlineColor{0.0f};
            float outlineWidth = 0.0f;
            bool clipped = false;
        };

        struct FontAtlas
        {
            GLuint textureId = 0;
            int width = 0;
            int height = 0;
            float fontSize = 0.0f;
            stbtt_bakedchar glyphs[kBakedCodepointCount]{};
        };

        struct TextGlyphQuad
        {
            UIRect rect;
            glm::vec2 uvMin{0.0f};
            glm::vec2 uvMax{0.0f};
            FontAtlas *fontAtlas = nullptr;
            glm::vec4 color{1.0f};
            float italicOffset = 0.0f;
            float boldOffset = 0.0f;
            glm::vec4 outlineColor{0.0f};
            float outlineWidth = 0.0f;
        };

        struct ActiveCanvas
        {
            const scene::CanvasComponent *component = nullptr;
            std::optional<scene::RectTransformLayout> parentRect;
            std::optional<UIRect> clipRect;
            float opacity = 1.0f;
        };

        struct ProjectedWorldPoint
        {
            glm::vec2 screenPosition{0.0f};
            float depth = 0.0f;
            float pixelsPerWorldUnit = 1.0f;
        };

        Shader *CreateRuntimeUIShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                layout(location = 0) in vec4 aRect;
                layout(location = 1) in vec4 aUv;
                layout(location = 2) in vec4 aColor;
                layout(location = 3) in vec4 aShape;
                layout(location = 4) in vec4 aTransform;
                layout(location = 5) in vec2 aDepth;
                uniform vec2 uViewportSize;
                out vec2 vUv;
                out vec2 vLocal;
                out vec4 vColor;
                flat out int vImageType;
                flat out float vFillAmount;
                flat out float vThickness;
                flat out float vCornerRadius;
                flat out float vStartAngle;
                flat out float vElementDepth;

                void main()
                {
                    vec2 uRectMin = aRect.xy;
                    vec2 uRectMax = aRect.zw;
                    vec2 uUvMin = aUv.xy;
                    vec2 uUvMax = aUv.zw;
                    vec2 positions[6] = vec2[6](
                        vec2(uRectMin.x, uRectMin.y),
                        vec2(uRectMax.x, uRectMin.y),
                        vec2(uRectMax.x, uRectMax.y),
                        vec2(uRectMin.x, uRectMin.y),
                        vec2(uRectMax.x, uRectMax.y),
                        vec2(uRectMin.x, uRectMax.y)
                    );
                    vec2 uvs[6] = vec2[6](
                        vec2(uUvMin.x, uUvMin.y),
                        vec2(uUvMax.x, uUvMin.y),
                        vec2(uUvMax.x, uUvMax.y),
                        vec2(uUvMin.x, uUvMin.y),
                        vec2(uUvMax.x, uUvMax.y),
                        vec2(uUvMin.x, uUvMax.y)
                    );

                    vec2 center = (uRectMin + uRectMax) * 0.5;
                    vec2 local = positions[gl_VertexID] - center;
                    local *= aTransform.zw;
                    float angle = radians(aTransform.y);
                    mat2 rotation = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
                    positions[gl_VertexID] = center + rotation * local;
                    vec2 ndc = (positions[gl_VertexID] / max(uViewportSize, vec2(1.0))) * 2.0 - 1.0;
                    gl_Position = vec4(ndc, 0.0, 1.0);
                    vUv = uvs[gl_VertexID];
                    vLocal = uvs[gl_VertexID] * 2.0 - 1.0;
                    vColor = aColor;
                    vImageType = int(aShape.x + 0.5);
                    vFillAmount = aShape.y;
                    vThickness = aShape.z;
                    vCornerRadius = aShape.w;
                    vStartAngle = aTransform.x;
                    vElementDepth = aDepth.x;
                }
            )";

            source.fragmentSource = R"(
                #version 330 core
                out vec4 FragColor;
                uniform sampler2D uImageTexture;
                uniform int uHasTexture;
                uniform sampler2D uSceneDepthTexture;
                uniform vec2 uViewportSize;
                uniform int uDepthTest;
                in vec2 vUv;
                in vec2 vLocal;
                in vec4 vColor;
                flat in int vImageType;
                flat in float vFillAmount;
                flat in float vThickness;
                flat in float vCornerRadius;
                flat in float vStartAngle;
                flat in float vElementDepth;

                float sdRoundedBox(vec2 p, vec2 b, float r)
                {
                    vec2 q = abs(p) - b + r;
                    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
                }

                void main()
                {
                    if (uDepthTest != 0)
                    {
                        float sceneDepth = texture(uSceneDepthTexture, gl_FragCoord.xy / max(uViewportSize, vec2(1.0))).r;
                        if (vElementDepth > sceneDepth + 0.00005)
                        {
                            discard;
                        }
                    }

                    if (vImageType == 3 && vUv.y > vFillAmount)
                        discard;
                    if (vImageType == 4 || vImageType == 6 || vImageType == 7)
                    {
                        float angle = mod(atan(vLocal.y, vLocal.x) + 6.2831853 - radians(vStartAngle), 6.2831853);
                        if ((vImageType == 4 || vImageType == 7) && angle > vFillAmount * 6.2831853)
                            discard;
                        float radius = length(vLocal);
                        float width = max(vThickness * 0.01, 0.002);
                        if (radius > 1.0 || radius < 1.0 - width)
                            discard;
                    }
                    else if (vImageType == 5)
                    {
                        float width = max(vThickness * 0.01, 0.002);
                        float gap = clamp(vFillAmount, 0.0, 0.95);
                        bool horizontal = abs(vLocal.y) <= width && abs(vLocal.x) >= gap;
                        bool vertical = abs(vLocal.x) <= width && abs(vLocal.y) >= gap;
                        if (!horizontal && !vertical)
                            discard;
                    }
                    else if (vImageType == 8)
                    {
                        float radius = clamp(vCornerRadius * 0.01, 0.0, 1.0);
                        float distance = sdRoundedBox(vLocal, vec2(1.0), radius);
                        float width = max(vThickness * 0.01, 0.002);
                        if (distance > 0.0 || (vThickness > 0.0 && distance < -width))
                            discard;
                    }
                    vec4 imageColor = uHasTexture != 0 ? texture(uImageTexture, vUv) : vec4(1.0);
                    FragColor = imageColor * vColor;
                }
            )";

            return Shader::Create(source);
        }

        Shader *CreateRuntimeUITextShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                uniform vec2 uViewportSize;
                uniform vec2 uRectMin;
                uniform vec2 uRectMax;
                uniform vec2 uUvMin;
                uniform vec2 uUvMax;
                uniform float uItalicOffset;

                out vec2 vUv;

                void main()
                {
                    vec2 positions[6] = vec2[6](
                        vec2(uRectMin.x, uRectMin.y),
                        vec2(uRectMax.x, uRectMin.y),
                        vec2(uRectMax.x, uRectMax.y),
                        vec2(uRectMin.x, uRectMin.y),
                        vec2(uRectMax.x, uRectMax.y),
                        vec2(uRectMin.x, uRectMax.y)
                    );

                    float topVertices[6] = float[6](0.0, 0.0, 1.0, 0.0, 1.0, 1.0);
                    positions[gl_VertexID].x += topVertices[gl_VertexID] * uItalicOffset;

                    vec2 uvs[6] = vec2[6](
                        vec2(uUvMin.x, uUvMax.y),
                        vec2(uUvMax.x, uUvMax.y),
                        vec2(uUvMax.x, uUvMin.y),
                        vec2(uUvMin.x, uUvMax.y),
                        vec2(uUvMax.x, uUvMin.y),
                        vec2(uUvMin.x, uUvMin.y)
                    );

                    vec2 ndc = (positions[gl_VertexID] / max(uViewportSize, vec2(1.0))) * 2.0 - 1.0;
                    gl_Position = vec4(ndc, 0.0, 1.0);
                    vUv = uvs[gl_VertexID];
                }
            )";

            source.fragmentSource = R"(
                #version 330 core
                in vec2 vUv;
                out vec4 FragColor;

                uniform sampler2D uFontAtlas;
                uniform sampler2D uSceneDepthTexture;
                uniform vec4 uColor;
                uniform vec2 uViewportSize;
                uniform float uElementDepth;
                uniform int uDepthTest;

                void main()
                {
                    if (uDepthTest != 0)
                    {
                        float sceneDepth = texture(uSceneDepthTexture, gl_FragCoord.xy / max(uViewportSize, vec2(1.0))).r;
                        if (uElementDepth > sceneDepth + 0.00005)
                        {
                            discard;
                        }
                    }

                    float alpha = texture(uFontAtlas, vUv).r;
                    if (alpha <= 0.001)
                    {
                        discard;
                    }

                    FragColor = vec4(uColor.rgb, uColor.a * alpha);
                }
            )";

            return Shader::Create(source);
        }

        UIRect ResolveWorldOverlayRect(const scene::RectTransformComponent &rectTransform,
                                       const glm::vec2 &screenOrigin,
                                       float pixelScale)
        {
            const glm::vec2 size = glm::max(rectTransform.GetSizeDelta(), glm::vec2(0.0f));
            const glm::vec2 localMin = rectTransform.GetAnchoredPosition() - size * rectTransform.GetPivot();
            const glm::vec2 min = screenOrigin + localMin * pixelScale;
            return UIRect{.min = min, .max = min + size * pixelScale};
        }

        bool ProjectWorldPosition(const glm::vec3 &worldPosition,
                                  const glm::mat4 &view,
                                  const glm::mat4 &projection,
                                  const glm::vec2 &viewportSize,
                                  ProjectedWorldPoint &projectedPoint)
        {
            const glm::vec4 viewPosition = view * glm::vec4(worldPosition, 1.0f);
            const glm::vec4 clipPosition = projection * viewPosition;
            if (clipPosition.w <= 0.0001f)
            {
                return false;
            }

            const glm::vec3 ndc = glm::vec3(clipPosition) / clipPosition.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f)
            {
                return false;
            }

            projectedPoint.screenPosition = (glm::vec2(ndc) * 0.5f + 0.5f) * viewportSize;
            projectedPoint.depth = ndc.z * 0.5f + 0.5f;
            projectedPoint.pixelsPerWorldUnit = std::abs(projection[1][1]) * viewportSize.y * 0.5f;
            if (std::abs(projection[3][3]) < 0.5f)
            {
                projectedPoint.pixelsPerWorldUnit /= std::max(-viewPosition.z, 0.0001f);
            }
            return true;
        }

        glm::vec4 ResolveButtonColor(const scene::UIButtonComponent *button, glm::vec4 color)
        {
            if (!button)
            {
                return color;
            }
            return color * button->GetCurrentTint();
        }

        std::vector<unsigned char> ReadBinaryFile(const std::filesystem::path &filePath)
        {
            std::ifstream input(filePath, std::ios::binary);
            if (!input.is_open())
            {
                return {};
            }

            input.seekg(0, std::ios::end);
            const auto size = input.tellg();
            if (size <= 0)
            {
                return {};
            }

            std::vector<unsigned char> data(static_cast<std::size_t>(size));
            input.seekg(0, std::ios::beg);
            input.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!input)
            {
                return {};
            }

            return data;
        }

        std::filesystem::path ResolveFontPath(const std::string &requestedFontPath)
        {
            if (!requestedFontPath.empty())
            {
                std::filesystem::path requestedPath(requestedFontPath);
                if (std::filesystem::exists(requestedPath))
                {
                    return requestedPath;
                }
            }

            const std::filesystem::path candidates[] = {
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/arial.ttf",
                "C:/Windows/Fonts/calibri.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
                "/System/Library/Fonts/Supplemental/Arial.ttf",
            };

            for (const auto &candidate : candidates)
            {
                if (std::filesystem::exists(candidate))
                {
                    return candidate;
                }
            }

            return {};
        }

        std::string MakeFontAtlasKey(const std::filesystem::path &fontPath, float fontSize)
        {
            return fontPath.lexically_normal().string() + "#" + std::to_string(static_cast<int>(std::round(fontSize)));
        }

        FontAtlas *GetOrCreateFontAtlas(const std::string &requestedFontPath, float requestedFontSize)
        {
            static std::unordered_map<std::string, std::unique_ptr<FontAtlas>> fontAtlases;

            const auto fontPath = ResolveFontPath(requestedFontPath);
            if (fontPath.empty())
            {
                return nullptr;
            }

            const float fontSize = std::clamp(requestedFontSize, 8.0f, 128.0f);
            const auto key = MakeFontAtlasKey(fontPath, fontSize);
            if (auto atlasIt = fontAtlases.find(key); atlasIt != fontAtlases.end())
            {
                return atlasIt->second.get();
            }

            const auto fontData = ReadBinaryFile(fontPath);
            if (fontData.empty())
            {
                return nullptr;
            }

            const int atlasSize = fontSize > 72.0f ? 2048 : 1024;
            std::vector<unsigned char> pixels(static_cast<std::size_t>(atlasSize * atlasSize), 0);

            auto atlas = std::make_unique<FontAtlas>();
            atlas->width = atlasSize;
            atlas->height = atlasSize;
            atlas->fontSize = fontSize;

            const int bakeResult = stbtt_BakeFontBitmap(fontData.data(),
                                                        0,
                                                        fontSize,
                                                        pixels.data(),
                                                        atlasSize,
                                                        atlasSize,
                                                        kFirstBakedCodepoint,
                                                        kBakedCodepointCount,
                                                        atlas->glyphs);
            if (bakeResult <= 0)
            {
                return nullptr;
            }

            glGenTextures(1, &atlas->textureId);
            glBindTexture(GL_TEXTURE_2D, atlas->textureId);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasSize, atlasSize, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glBindTexture(GL_TEXTURE_2D, 0);

            auto *atlasPtr = atlas.get();
            fontAtlases.emplace(key, std::move(atlas));
            return atlasPtr;
        }

        std::uint32_t DecodeUtf8Codepoint(const std::string &text, std::size_t &byteIndex)
        {
            const auto firstByte = static_cast<unsigned char>(text[byteIndex++]);
            if (firstByte < 0x80)
            {
                return firstByte;
            }

            const auto readContinuation = [&text, &byteIndex]() -> std::uint32_t
            {
                if (byteIndex >= text.size())
                {
                    return 0;
                }

                const auto byte = static_cast<unsigned char>(text[byteIndex]);
                if ((byte & 0xc0) != 0x80)
                {
                    return 0;
                }

                ++byteIndex;
                return byte & 0x3f;
            };

            if ((firstByte & 0xe0) == 0xc0)
            {
                const auto second = readContinuation();
                return second == 0 ? '?' : ((firstByte & 0x1f) << 6) | second;
            }

            if ((firstByte & 0xf0) == 0xe0)
            {
                const auto second = readContinuation();
                const auto third = readContinuation();
                return (second == 0 || third == 0) ? '?' : ((firstByte & 0x0f) << 12) | (second << 6) | third;
            }

            if ((firstByte & 0xf8) == 0xf0)
            {
                const auto second = readContinuation();
                const auto third = readContinuation();
                const auto fourth = readContinuation();
                return (second == 0 || third == 0 || fourth == 0) ? '?' : ((firstByte & 0x07) << 18) | (second << 12) | (third << 6) | fourth;
            }

            return '?';
        }

        std::uint32_t NormalizeRenderableCodepoint(std::uint32_t codepoint)
        {
            if (codepoint >= static_cast<std::uint32_t>(kFirstBakedCodepoint) &&
                codepoint < static_cast<std::uint32_t>(kFirstBakedCodepoint + kBakedCodepointCount))
            {
                return codepoint;
            }

            return '?';
        }

        struct RichTextStyle
        {
            glm::vec4 color{1.0f};
            float fontSize = 18.0f;
            bool bold = false;
            bool italic = false;
        };

        struct RichTextToken
        {
            std::uint32_t codepoint = 0;
            RichTextStyle style;
            bool lineBreak = false;
        };

        int HexDigit(char value)
        {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            if (value >= 'A' && value <= 'F')
                return value - 'A' + 10;
            return -1;
        }

        bool ParseRichTextColor(const std::string &value, glm::vec4 &color)
        {
            if ((value.size() != 7 && value.size() != 9) || value[0] != '#')
            {
                return false;
            }

            int channels[4] = {0, 0, 0, 255};
            const int channelCount = value.size() == 9 ? 4 : 3;
            for (int channel = 0; channel < channelCount; ++channel)
            {
                const int high = HexDigit(value[1 + channel * 2]);
                const int low = HexDigit(value[2 + channel * 2]);
                if (high < 0 || low < 0)
                {
                    return false;
                }
                channels[channel] = high * 16 + low;
            }

            color = glm::vec4(channels[0], channels[1], channels[2], channels[3]) / 255.0f;
            return true;
        }

        std::vector<RichTextToken> TokenizeRichText(const UITextRun &textRun)
        {
            struct StyleFrame
            {
                std::string tag;
                RichTextStyle style;
            };

            std::vector<RichTextToken> tokens;
            std::vector<StyleFrame> styles{{"", RichTextStyle{.color = textRun.color, .fontSize = textRun.fontSize}}};

            for (std::size_t byteIndex = 0; byteIndex < textRun.text.size();)
            {
                if (textRun.richText && textRun.text[byteIndex] == '<')
                {
                    const auto tagEnd = textRun.text.find('>', byteIndex + 1);
                    if (tagEnd != std::string::npos)
                    {
                        const std::string tag = textRun.text.substr(byteIndex + 1, tagEnd - byteIndex - 1);
                        bool recognized = false;
                        if (tag == "br" || tag == "br/")
                        {
                            tokens.push_back(RichTextToken{.style = styles.back().style, .lineBreak = true});
                            recognized = true;
                        }
                        else if (tag == "b" || tag == "i" || tag.rfind("color=", 0) == 0 || tag.rfind("size=", 0) == 0)
                        {
                            RichTextStyle style = styles.back().style;
                            if (tag == "b")
                            {
                                style.bold = true;
                                recognized = true;
                            }
                            else if (tag == "i")
                            {
                                style.italic = true;
                                recognized = true;
                            }
                            else if (tag.rfind("color=", 0) == 0)
                            {
                                glm::vec4 parsedColor;
                                recognized = ParseRichTextColor(tag.substr(6), parsedColor);
                                if (recognized)
                                {
                                    style.color = parsedColor;
                                }
                            }
                            else
                            {
                                try
                                {
                                    style.fontSize = std::clamp(std::stof(tag.substr(5)), 8.0f, 128.0f);
                                    recognized = true;
                                }
                                catch (...)
                                {
                                }
                            }

                            if (recognized)
                            {
                                styles.push_back(StyleFrame{.tag = tag.substr(0, tag.find('=')), .style = style});
                            }
                        }
                        else if (tag.size() > 1 && tag[0] == '/')
                        {
                            const std::string closingTag = tag.substr(1);
                            if (styles.size() > 1 && styles.back().tag == closingTag)
                            {
                                styles.pop_back();
                                recognized = true;
                            }
                        }

                        if (recognized)
                        {
                            byteIndex = tagEnd + 1;
                            continue;
                        }
                    }
                }

                const auto codepoint = DecodeUtf8Codepoint(textRun.text, byteIndex);
                if (codepoint == '\r')
                {
                    continue;
                }
                if (codepoint == '\n')
                {
                    tokens.push_back(RichTextToken{.style = styles.back().style, .lineBreak = true});
                }
                else
                {
                    tokens.push_back(RichTextToken{.codepoint = NormalizeRenderableCodepoint(codepoint), .style = styles.back().style});
                }
            }

            return tokens;
        }

        std::vector<TextGlyphQuad> BuildTextGlyphQuads(const UITextRun &textRun)
        {
            std::vector<TextGlyphQuad> glyphQuads;
            if (textRun.text.empty() || textRun.pixelScale <= 0.0001f)
            {
                return glyphQuads;
            }

            const auto tokens = TokenizeRichText(textRun);
            std::vector<float> lineHeights(1, std::clamp(textRun.fontSize, 8.0f, 128.0f));
            for (const auto &token : tokens)
            {
                if (token.lineBreak)
                {
                    lineHeights.push_back(std::clamp(textRun.fontSize, 8.0f, 128.0f));
                }
                else
                {
                    lineHeights.back() = std::max(lineHeights.back(), token.style.fontSize);
                }
            }

            float penX = 0.0f;
            float lineTop = 0.0f;
            std::size_t lineIndex = 0;
            const float rectHeight = std::max(textRun.rect.max.y - textRun.rect.min.y, 0.0f) / textRun.pixelScale;
            const float rectWidth = std::max(textRun.rect.max.x - textRun.rect.min.x, 0.0f) / textRun.pixelScale;

            for (const auto &token : tokens)
            {
                if (token.lineBreak)
                {
                    penX = 0.0f;
                    lineTop += lineHeights[lineIndex] * 1.25f * textRun.lineSpacing;
                    ++lineIndex;
                    continue;
                }

                if (lineIndex >= lineHeights.size() || lineTop > rectHeight)
                {
                    break;
                }

                auto *atlas = GetOrCreateFontAtlas(textRun.fontPath, token.style.fontSize);
                if (!atlas || atlas->textureId == 0)
                {
                    continue;
                }

                float nextPenX = penX;
                float nextPenY = lineTop + lineHeights[lineIndex];
                stbtt_aligned_quad bakedQuad{};
                stbtt_GetBakedQuad(atlas->glyphs,
                                   atlas->width,
                                   atlas->height,
                                   static_cast<int>(token.codepoint) - kFirstBakedCodepoint,
                                   &nextPenX,
                                   &nextPenY,
                                   &bakedQuad,
                                   1);

                if (textRun.wrap && penX > 0.0f && nextPenX > rectWidth)
                {
                    penX = 0.0f;
                    lineTop += lineHeights[lineIndex] * 1.25f * textRun.lineSpacing;
                    nextPenX = penX;
                    nextPenY = lineTop + lineHeights[lineIndex];
                    stbtt_GetBakedQuad(atlas->glyphs, atlas->width, atlas->height,
                                       static_cast<int>(token.codepoint) - kFirstBakedCodepoint,
                                       &nextPenX, &nextPenY, &bakedQuad, 1);
                }

                const glm::vec2 min(textRun.rect.min.x + bakedQuad.x0 * textRun.pixelScale,
                                    textRun.rect.max.y - bakedQuad.y1 * textRun.pixelScale);
                const glm::vec2 max(textRun.rect.min.x + bakedQuad.x1 * textRun.pixelScale,
                                    textRun.rect.max.y - bakedQuad.y0 * textRun.pixelScale);
                if (max.x > textRun.rect.min.x && min.x < textRun.rect.max.x &&
                    max.y > textRun.rect.min.y && min.y < textRun.rect.max.y)
                {
                    glyphQuads.push_back(TextGlyphQuad{
                        .rect = UIRect{.min = min, .max = max},
                        .uvMin = glm::vec2(bakedQuad.s0, bakedQuad.t0),
                        .uvMax = glm::vec2(bakedQuad.s1, bakedQuad.t1),
                        .fontAtlas = atlas,
                        .color = token.style.color,
                        .italicOffset = token.style.italic ? token.style.fontSize * textRun.pixelScale * 0.18f : 0.0f,
                        .boldOffset = token.style.bold ? std::max(textRun.pixelScale * 0.75f, 0.35f) : 0.0f,
                        .outlineColor = textRun.outlineColor,
                        .outlineWidth = textRun.outlineWidth,
                    });
                }

                penX = nextPenX;
            }

            if (!glyphQuads.empty())
            {
                glm::vec2 boundsMin = glyphQuads.front().rect.min;
                glm::vec2 boundsMax = glyphQuads.front().rect.max;
                for (const auto &glyph : glyphQuads)
                {
                    boundsMin = glm::min(boundsMin, glyph.rect.min);
                    boundsMax = glm::max(boundsMax, glyph.rect.max);
                }
                glm::vec2 offset(0.0f);
                const int alignment = static_cast<int>(textRun.alignment);
                const int column = alignment % 3;
                const int row = alignment / 3;
                if (column == 1) offset.x = ((textRun.rect.min.x + textRun.rect.max.x) - (boundsMin.x + boundsMax.x)) * 0.5f;
                else if (column == 2) offset.x = textRun.rect.max.x - boundsMax.x;
                if (row == 1) offset.y = ((textRun.rect.min.y + textRun.rect.max.y) - (boundsMin.y + boundsMax.y)) * 0.5f;
                else if (row == 0) offset.y = textRun.rect.max.y - boundsMax.y;
                else offset.y = textRun.rect.min.y - boundsMin.y;
                for (auto &glyph : glyphQuads)
                {
                    glyph.rect.min += offset;
                    glyph.rect.max += offset;
                }
            }

            return glyphQuads;
        }

        void CollectUIQuads(scene::Entity *entity,
                            ActiveCanvas activeCanvas,
                            const glm::vec2 &viewportSize,
                            const glm::mat4 &view,
                            const glm::mat4 &projection,
                            std::vector<UIQuad> &quads,
                            std::vector<UITextRun> &textRuns,
                            std::optional<scene::RectTransformLayout> layoutOverride = std::nullopt)
        {
            if (!entity || !entity->IsActive())
            {
                return;
            }

            if (auto *canvas = entity->GetComponent<scene::CanvasComponent>(); canvas && canvas->IsEnabled())
            {
                const bool simpleRmlText = canvas->GetBackend() == scene::UIRenderBackend::RmlUi &&
                                           canvas->GetContentSource() == scene::RmlUiContentSource::Text &&
                                           canvas->GetRenderMode() != scene::CanvasRenderMode::WorldSpace;
                if (canvas->GetBackend() == scene::UIRenderBackend::RmlUi && !simpleRmlText)
                    return;
                activeCanvas.component = canvas;
                const float scaleFactor = simpleRmlText ? 1.0f : scene::ResolveCanvasScaleFactor(*canvas, viewportSize);
                activeCanvas.parentRect = scene::RectTransformLayout{
                    .min = glm::vec2(0.0f),
                    .max = viewportSize / scaleFactor,
                };
                activeCanvas.clipRect = UIRect{.min = glm::vec2(0.0f), .max = viewportSize};
                activeCanvas.opacity = 1.0f;
            }

            auto *rectTransform = entity->GetComponent<scene::RectTransformComponent>();
            auto *image = entity->GetComponent<scene::UIImageComponent>();
            auto *button = entity->GetComponent<scene::UIButtonComponent>();
            auto *text = entity->GetComponent<scene::UITextComponent>();
            if (activeCanvas.component && rectTransform && rectTransform->IsEnabled())
            {
                const bool simpleRmlText = activeCanvas.component->GetBackend() == scene::UIRenderBackend::RmlUi &&
                                           activeCanvas.component->GetContentSource() == scene::RmlUiContentSource::Text;
                const float scaleFactor = simpleRmlText ? 1.0f : scene::ResolveCanvasScaleFactor(*activeCanvas.component, viewportSize);
                const bool worldSpaceOverlay =
                    activeCanvas.component->GetRenderMode() == scene::CanvasRenderMode::WorldSpaceOverlay ||
                    activeCanvas.component->GetRenderMode() == scene::CanvasRenderMode::WorldSpace;
                ProjectedWorldPoint projectedPoint;
                const bool visible = !worldSpaceOverlay || ProjectWorldPosition(entity->GetWorldPosition(),
                                                                                 view,
                                                                                 projection,
                                                                                 viewportSize,
                                                                                 projectedPoint);
                float pixelScale = 1.0f;
                if (worldSpaceOverlay)
                {
                    const glm::vec3 worldScale = glm::abs(entity->GetWorldScale());
                    const float uniformWorldScale = std::max(worldScale.x, worldScale.y);
                    pixelScale = simpleRmlText ? uniformWorldScale
                                               : projectedPoint.pixelsPerWorldUnit * uniformWorldScale * scaleFactor / kUIUnitsPerWorldUnit;
                }
                auto logicalLayout = activeCanvas.parentRect
                                               ? (layoutOverride ? *layoutOverride : scene::ResolveRectTransformLayout(*rectTransform, *activeCanvas.parentRect))
                                               : scene::RectTransformLayout{};
                std::vector<const scene::RectTransformComponent *> layoutChildren;
                for (const auto *childEntity : entity->GetChildren())
                    if (const auto *childRect = childEntity->GetComponent<scene::RectTransformComponent>(); childRect && childRect->IsEnabled())
                        layoutChildren.push_back(childRect);
                if (!layoutChildren.empty() &&
                    (rectTransform->GetHorizontalContentSize() != scene::UIContentSizeMode::Unconstrained ||
                     rectTransform->GetVerticalContentSize() != scene::UIContentSizeMode::Unconstrained))
                {
                    const glm::vec2 preferred = scene::ResolvePreferredLayoutSize(*rectTransform, layoutChildren);
                    glm::vec2 size = logicalLayout.max - logicalLayout.min;
                    if (rectTransform->GetHorizontalContentSize() == scene::UIContentSizeMode::Preferred) size.x = preferred.x;
                    else if (rectTransform->GetHorizontalContentSize() == scene::UIContentSizeMode::Minimum) size.x = rectTransform->GetMinimumSize().x;
                    if (rectTransform->GetVerticalContentSize() == scene::UIContentSizeMode::Preferred) size.y = preferred.y;
                    else if (rectTransform->GetVerticalContentSize() == scene::UIContentSizeMode::Minimum) size.y = rectTransform->GetMinimumSize().y;
                    const glm::vec2 pivotPoint = glm::mix(logicalLayout.min, logicalLayout.max, rectTransform->GetPivot());
                    logicalLayout = {.min = pivotPoint - size * rectTransform->GetPivot(),
                                     .max = pivotPoint + size * (glm::vec2(1.0f) - rectTransform->GetPivot())};
                }
                auto rect = worldSpaceOverlay
                                ? ResolveWorldOverlayRect(*rectTransform, projectedPoint.screenPosition, pixelScale)
                                : UIRect{.min = logicalLayout.min, .max = logicalLayout.max};
                if (!worldSpaceOverlay)
                {
                    rect.min *= scaleFactor;
                    rect.max *= scaleFactor;
                }
                const float elementOpacity = activeCanvas.opacity * rectTransform->GetOpacity();
                const UIRect effectiveClip = activeCanvas.clipRect
                                                 ? IntersectRects(*activeCanvas.clipRect, rect)
                                                 : rect;
                const bool clipped = effectiveClip.max.x <= effectiveClip.min.x ||
                                     effectiveClip.max.y <= effectiveClip.min.y;

                if (visible && ((image && image->IsEnabled()) || (button && button->IsEnabled())))
                {
                    glm::vec4 color = image && image->IsEnabled() ? image->GetColor() : glm::vec4(0.16f, 0.18f, 0.22f, 0.92f);
                    color.a *= elementOpacity;
                    render::Texture *texture = nullptr;
                    float fillAmount = 1.0f;
                    if (image && image->IsEnabled())
                    {
                        fillAmount = image->GetFillAmount();
                        if (!image->GetTexturePath().empty())
                        {
                            auto &engine = core::Engine::GetInstance();
                            std::string resolvedPath = engine.GetAssetManager().ResolveAssetPath(image->GetTexturePath());
                            if (resolvedPath.empty())
                            {
                                resolvedPath = image->GetTexturePath();
                            }
                            texture = engine.GetTextureManager().LoadTextureFromFile(resolvedPath.c_str());
                        }
                    }

                    if (texture && image->GetPreserveAspect() && texture->GetWidth() > 0 && texture->GetHeight() > 0)
                    {
                        const glm::vec2 rectSize = rect.max - rect.min;
                        const float textureAspect = static_cast<float>(texture->GetWidth()) / static_cast<float>(texture->GetHeight());
                        const float rectAspect = rectSize.x / std::max(rectSize.y, 0.0001f);
                        if (rectAspect > textureAspect)
                        {
                            const float width = rectSize.y * textureAspect;
                            const float inset = (rectSize.x - width) * 0.5f;
                            rect.min.x += inset;
                            rect.max.x -= inset;
                        }
                        else
                        {
                            const float height = rectSize.x / std::max(textureAspect, 0.0001f);
                            const float inset = (rectSize.y - height) * 0.5f;
                            rect.min.y += inset;
                            rect.max.y -= inset;
                        }
                    }

                    if (fillAmount <= 0.0f)
                    {
                        color.a = 0.0f;
                    }
                    const auto imageType = image ? image->GetImageType() : scene::UIImageType::Simple;
                    if (imageType == scene::UIImageType::FilledHorizontal)
                        rect.max.x = glm::mix(rect.min.x, rect.max.x, fillAmount);
                    UIQuad baseQuad{
                        .rect = rect,
                        .clipRect = activeCanvas.clipRect.value_or(rect),
                        .uvMax = glm::vec2(fillAmount, 1.0f),
                        .color = ResolveButtonColor(button, color),
                        .textureId = texture ? texture->GetTextureID() : 0,
                        .depth = projectedPoint.depth,
                        .depthTest = worldSpaceOverlay && !simpleRmlText,
                        .sortingOrder = activeCanvas.component->GetSortingOrder(),
                        .entityId = entity->GetID(),
                        .imageType = imageType,
                        .fillAmount = fillAmount,
                        .thickness = image ? image->GetThickness() : 2.0f,
                        .cornerRadius = image ? image->GetCornerRadius() : 0.0f,
                        .startAngle = image ? image->GetStartAngle() : 0.0f,
                        .rotation = rectTransform->GetRotation(),
                        .localScale = rectTransform->GetLocalScale(),
                        .clipped = clipped,
                    };
                    if (imageType == scene::UIImageType::Sliced && image && texture &&
                        texture->GetWidth() > 0 && texture->GetHeight() > 0)
                    {
                        const glm::vec4 border = image->GetBorder();
                        const glm::vec2 rectSize = glm::max(rect.max - rect.min, glm::vec2(0.0f));
                        const float left = std::min(border.x, rectSize.x * 0.5f);
                        const float bottom = std::min(border.y, rectSize.y * 0.5f);
                        const float right = std::min(border.z, rectSize.x * 0.5f);
                        const float top = std::min(border.w, rectSize.y * 0.5f);
                        const float xs[4] = {rect.min.x, rect.min.x + left, rect.max.x - right, rect.max.x};
                        const float ys[4] = {rect.min.y, rect.min.y + bottom, rect.max.y - top, rect.max.y};
                        const float us[4] = {0.0f, border.x / texture->GetWidth(),
                                             1.0f - border.z / texture->GetWidth(), 1.0f};
                        const float vs[4] = {0.0f, border.y / texture->GetHeight(),
                                             1.0f - border.w / texture->GetHeight(), 1.0f};
                        for (int y = 0; y < 3; ++y)
                        {
                            for (int x = 0; x < 3; ++x)
                            {
                                auto slice = baseQuad;
                                slice.rect = UIRect{.min = glm::vec2(xs[x], ys[y]),
                                                    .max = glm::vec2(xs[x + 1], ys[y + 1])};
                                slice.uvMin = glm::vec2(us[x], vs[y]);
                                slice.uvMax = glm::vec2(us[x + 1], vs[y + 1]);
                                slice.imageType = scene::UIImageType::Simple;
                                quads.push_back(slice);
                            }
                        }
                    }
                    else
                    {
                        quads.push_back(baseQuad);
                    }
                }

                if (visible && text && text->IsEnabled() && !text->GetText().empty())
                {
                    textRuns.push_back(UITextRun{
                        .rect = rect,
                        .clipRect = activeCanvas.clipRect.value_or(rect),
                        .text = text->GetText(),
                        .fontPath = text->GetFontPath(),
                        .color = glm::vec4(glm::vec3(text->GetColor()), text->GetColor().a * elementOpacity),
                        .fontSize = worldSpaceOverlay ? text->GetFontSize() : text->GetFontSize() * scaleFactor,
                        .pixelScale = worldSpaceOverlay ? pixelScale : 1.0f,
                        .depth = projectedPoint.depth,
                        .depthTest = worldSpaceOverlay,
                        .richText = text->IsRichText(),
                        .sortingOrder = activeCanvas.component->GetSortingOrder(),
                        .entityId = entity->GetID(),
                        .alignment = text->GetAlignment(),
                        .wrap = text->GetWrap(),
                        .lineSpacing = text->GetLineSpacing(),
                        .outlineColor = text->GetOutlineColor(),
                        .outlineWidth = text->GetOutlineWidth(),
                        .clipped = clipped,
                    });
                }

                if (!worldSpaceOverlay)
                {
                    activeCanvas.parentRect = logicalLayout;
                    activeCanvas.opacity = elementOpacity;
                    if (rectTransform->GetClipChildren())
                        activeCanvas.clipRect = effectiveClip;
                }
            }

            std::vector<const scene::RectTransformComponent *> layoutChildren;
            std::vector<scene::Entity *> layoutEntities;
            for (auto *child : entity->GetChildren())
            {
                if (const auto *childRect = child->GetComponent<scene::RectTransformComponent>(); childRect && childRect->IsEnabled())
                {
                    layoutChildren.push_back(childRect);
                    layoutEntities.push_back(child);
                }
            }
            for (auto *child : entity->GetChildren())
            {
                std::optional<scene::RectTransformLayout> childOverride;
                if (rectTransform && activeCanvas.parentRect && rectTransform->GetLayoutMode() != scene::UILayoutMode::None)
                {
                    const auto found = std::find(layoutEntities.begin(), layoutEntities.end(), child);
                    if (found != layoutEntities.end())
                        childOverride = scene::ResolveAutomaticChildLayout(
                            *rectTransform,
                            *activeCanvas.parentRect,
                            layoutChildren,
                            static_cast<std::size_t>(found - layoutEntities.begin()));
                }
                CollectUIQuads(child, activeCanvas, viewportSize, view, projection, quads, textRuns, childOverride);
            }
        }
    }

    void RuntimeUIPass::Initialize()
    {
        m_shader = CreateRuntimeUIShader();
        m_textShader = CreateRuntimeUITextShader();
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_instanceVbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
        const GLsizei stride = static_cast<GLsizei>(sizeof(UIQuadInstance));
        const std::size_t offsets[] = {
            offsetof(UIQuadInstance, rect), offsetof(UIQuadInstance, uv), offsetof(UIQuadInstance, color),
            offsetof(UIQuadInstance, shape), offsetof(UIQuadInstance, transform), offsetof(UIQuadInstance, depth)};
        const GLint sizes[] = {4, 4, 4, 4, 4, 2};
        for (GLuint attribute = 0; attribute < 6; ++attribute)
        {
            glEnableVertexAttribArray(attribute);
            glVertexAttribPointer(attribute, sizes[attribute], GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void *>(offsets[attribute]));
            glVertexAttribDivisor(attribute, 1);
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void RuntimeUIPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.scene || !ctx.scene->HasNativeRuntimeUI() || !m_shader || !m_textShader || m_vao == 0)
        {
            return;
        }

        int renderWidth = 0;
        int renderHeight = 0;
        if (ctx.renderTarget)
        {
            renderWidth = ctx.renderTarget->GetWidth();
            renderHeight = ctx.renderTarget->GetHeight();
            Graphics::BindRenderTarget(ctx.renderTarget);
        }
        else if (ctx.temporaryRenderTarget)
        {
            renderWidth = ctx.temporaryRenderTarget->GetWidth();
            renderHeight = ctx.temporaryRenderTarget->GetHeight();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        if (renderWidth <= 0 || renderHeight <= 0)
        {
            return;
        }

        std::vector<UIQuad> quads;
        std::vector<UITextRun> textRuns;
        const glm::vec2 viewportSize(static_cast<float>(renderWidth), static_cast<float>(renderHeight));
        for (auto *rootEntity : ctx.scene->GetRootEntities())
        {
            CollectUIQuads(rootEntity,
                           ActiveCanvas{},
                           viewportSize,
                           ctx.unjitteredCameraData.view,
                           ctx.unjitteredCameraData.projection,
                           quads,
                           textRuns);
        }

        if (quads.empty() && textRuns.empty())
        {
            return;
        }

        std::stable_sort(quads.begin(), quads.end(),
                         [](const UIQuad &a, const UIQuad &b)
                         {
                             if (a.sortingOrder != b.sortingOrder)
                             {
                                 return a.sortingOrder < b.sortingOrder;
                             }

                             return a.entityId < b.entityId;
                         });

        glViewport(0, 0, renderWidth, renderHeight);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glEnable(GL_SCISSOR_TEST);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        const GLuint sceneDepthTexture = ctx.temporaryRenderTarget ? ctx.temporaryRenderTarget->GetDepthTextureID() : 0;

        m_shader->Bind();
        m_shader->SetUniform("uViewportSize", viewportSize);
        m_shader->SetUniform("uImageTexture", 0);
        m_shader->SetUniform("uSceneDepthTexture", 1);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);

        glBindVertexArray(m_vao);
        for (std::size_t quadIndex = 0; quadIndex < quads.size();)
        {
            const auto &quad = quads[quadIndex];
            if (quad.clipped)
            {
                ++quadIndex;
                continue;
            }
            const bool depthTest = quad.depthTest && sceneDepthTexture != 0;
            const auto compatible = [&](const UIQuad &candidate)
            {
                return !candidate.clipped && candidate.textureId == quad.textureId &&
                       candidate.depthTest == quad.depthTest &&
                       candidate.clipRect.min == quad.clipRect.min &&
                       candidate.clipRect.max == quad.clipRect.max;
            };
            std::size_t groupEnd = quadIndex + 1;
            while (groupEnd < quads.size() && compatible(quads[groupEnd]))
                ++groupEnd;
            std::vector<UIQuadInstance> instances;
            instances.reserve(groupEnd - quadIndex);
            for (std::size_t instanceIndex = quadIndex; instanceIndex < groupEnd; ++instanceIndex)
            {
                const auto &item = quads[instanceIndex];
                instances.push_back({
                    .rect = {item.rect.min.x, item.rect.min.y, item.rect.max.x, item.rect.max.y},
                    .uv = {item.uvMin.x, item.uvMin.y, item.uvMax.x, item.uvMax.y},
                    .color = item.color,
                    .shape = {static_cast<float>(item.imageType), item.fillAmount, item.thickness, item.cornerRadius},
                    .transform = {item.startAngle, item.rotation, item.localScale.x, item.localScale.y},
                    .depth = {item.depth, 0.0f},
                });
            }
            const glm::vec2 clipSize = glm::max(quad.clipRect.max - quad.clipRect.min, glm::vec2(0.0f));
            glScissor(static_cast<GLint>(std::floor(quad.clipRect.min.x)),
                      static_cast<GLint>(std::floor(quad.clipRect.min.y)),
                      static_cast<GLsizei>(std::ceil(clipSize.x)),
                      static_cast<GLsizei>(std::ceil(clipSize.y)));
            m_shader->SetUniform("uHasTexture", quad.textureId != 0 ? 1 : 0);
            m_shader->SetUniform("uDepthTest", depthTest ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, quad.textureId);
            glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(instances.size() * sizeof(UIQuadInstance)),
                         instances.data(),
                         GL_STREAM_DRAW);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(instances.size()));
            quadIndex = groupEnd;
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        std::stable_sort(textRuns.begin(), textRuns.end(),
                         [](const UITextRun &a, const UITextRun &b)
                         {
                             if (a.sortingOrder != b.sortingOrder)
                             {
                                 return a.sortingOrder < b.sortingOrder;
                             }

                             return a.entityId < b.entityId;
                         });

        glEnable(GL_SCISSOR_TEST);
        m_textShader->Bind();
        m_textShader->SetUniform("uViewportSize", viewportSize);
        m_textShader->SetUniform("uFontAtlas", 0);
        m_textShader->SetUniform("uSceneDepthTexture", 1);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
        glActiveTexture(GL_TEXTURE0);
        for (const auto &textRun : textRuns)
        {
            if (textRun.clipped)
                continue;
            const glm::vec2 clipSize = glm::max(textRun.clipRect.max - textRun.clipRect.min, glm::vec2(0.0f));
            glScissor(static_cast<GLint>(std::floor(textRun.clipRect.min.x)),
                      static_cast<GLint>(std::floor(textRun.clipRect.min.y)),
                      static_cast<GLsizei>(std::ceil(clipSize.x)),
                      static_cast<GLsizei>(std::ceil(clipSize.y)));
            m_textShader->SetUniform("uElementDepth", textRun.depth);
            m_textShader->SetUniform("uDepthTest", textRun.depthTest && sceneDepthTexture != 0 ? 1 : 0);

            const auto glyphQuads = BuildTextGlyphQuads(textRun);
            for (const auto &glyphQuad : glyphQuads)
            {
                if (!glyphQuad.fontAtlas || glyphQuad.fontAtlas->textureId == 0)
                {
                    continue;
                }

                glBindTexture(GL_TEXTURE_2D, glyphQuad.fontAtlas->textureId);
                if (glyphQuad.outlineWidth > 0.0f && glyphQuad.outlineColor.a > 0.0f)
                {
                    m_textShader->SetUniform("uColor", glyphQuad.outlineColor);
                    constexpr glm::vec2 directions[] = {
                        {-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f},
                        {-0.707f, -0.707f}, {0.707f, -0.707f}, {-0.707f, 0.707f}, {0.707f, 0.707f}};
                    for (const auto &direction : directions)
                    {
                        const glm::vec2 offset = direction * glyphQuad.outlineWidth;
                        m_textShader->SetUniform("uRectMin", glyphQuad.rect.min + offset);
                        m_textShader->SetUniform("uRectMax", glyphQuad.rect.max + offset);
                        m_textShader->SetUniform("uUvMin", glyphQuad.uvMin);
                        m_textShader->SetUniform("uUvMax", glyphQuad.uvMax);
                        m_textShader->SetUniform("uItalicOffset", glyphQuad.italicOffset);
                        glDrawArrays(GL_TRIANGLES, 0, 6);
                    }
                }
                m_textShader->SetUniform("uColor", glyphQuad.color);
                m_textShader->SetUniform("uRectMin", glyphQuad.rect.min);
                m_textShader->SetUniform("uRectMax", glyphQuad.rect.max);
                m_textShader->SetUniform("uUvMin", glyphQuad.uvMin);
                m_textShader->SetUniform("uUvMax", glyphQuad.uvMax);
                m_textShader->SetUniform("uItalicOffset", glyphQuad.italicOffset);
                glDrawArrays(GL_TRIANGLES, 0, 6);

                if (glyphQuad.boldOffset > 0.0f)
                {
                    const glm::vec2 boldOffset(glyphQuad.boldOffset, 0.0f);
                    m_textShader->SetUniform("uRectMin", glyphQuad.rect.min + boldOffset);
                    m_textShader->SetUniform("uRectMax", glyphQuad.rect.max + boldOffset);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                }
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
    }
}
