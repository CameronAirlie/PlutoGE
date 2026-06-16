#include "PlutoGE/render/passes/RuntimeUIPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIComponent.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
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

        struct UIRect
        {
            glm::vec2 min{0.0f};
            glm::vec2 max{0.0f};
        };

        struct UIQuad
        {
            UIRect rect;
            glm::vec4 color{1.0f};
            int sortingOrder = 0;
            std::uint32_t entityId = 0;
        };

        struct UITextRun
        {
            UIRect rect;
            std::string text;
            std::string fontPath;
            glm::vec4 color{1.0f};
            float fontSize = 18.0f;
            int sortingOrder = 0;
            std::uint32_t entityId = 0;
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
        };

        Shader *CreateRuntimeUIShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                uniform vec2 uViewportSize;
                uniform vec2 uRectMin;
                uniform vec2 uRectMax;

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

                    vec2 ndc = (positions[gl_VertexID] / max(uViewportSize, vec2(1.0))) * 2.0 - 1.0;
                    gl_Position = vec4(ndc, 0.0, 1.0);
                }
            )";

            source.fragmentSource = R"(
                #version 330 core
                out vec4 FragColor;
                uniform vec4 uColor;

                void main()
                {
                    FragColor = uColor;
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
                uniform vec4 uColor;

                void main()
                {
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

        UIRect ResolveScreenRect(const scene::RectTransformComponent &rectTransform, const glm::vec2 &canvasSize)
        {
            const glm::vec2 anchorPosition = canvasSize * rectTransform.GetAnchorMin();
            const glm::vec2 size = glm::max(rectTransform.GetSizeDelta(), glm::vec2(0.0f));
            const glm::vec2 pivotOffset = size * rectTransform.GetPivot();
            const glm::vec2 min = anchorPosition + rectTransform.GetAnchoredPosition() - pivotOffset;
            return UIRect{.min = min, .max = min + size};
        }

        glm::vec4 ResolveButtonColor(const scene::UIButtonComponent *button, glm::vec4 color)
        {
            if (!button)
            {
                return color;
            }

            if (!button->IsInteractable())
            {
                color = glm::vec4(glm::vec3(color) * 0.45f, color.a * 0.75f);
            }
            else if (button->WasPressed())
            {
                color = glm::vec4(glm::vec3(color) * 0.72f, color.a);
            }
            else if (button->IsHovered())
            {
                color = glm::vec4(glm::min(glm::vec3(color) * 1.18f, glm::vec3(1.0f)), color.a);
            }

            return color;
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

        std::vector<TextGlyphQuad> BuildTextGlyphQuads(const UITextRun &textRun, const FontAtlas &atlas)
        {
            std::vector<TextGlyphQuad> glyphQuads;
            if (textRun.text.empty())
            {
                return glyphQuads;
            }

            float penX = 0.0f;
            float penY = atlas.fontSize;
            const float lineAdvance = atlas.fontSize * 1.25f;
            const float rectWidth = std::max(textRun.rect.max.x - textRun.rect.min.x, 0.0f);
            const float rectHeight = std::max(textRun.rect.max.y - textRun.rect.min.y, 0.0f);

            for (std::size_t byteIndex = 0; byteIndex < textRun.text.size();)
            {
                const auto rawCodepoint = DecodeUtf8Codepoint(textRun.text, byteIndex);
                if (rawCodepoint == '\r')
                {
                    continue;
                }

                if (rawCodepoint == '\n')
                {
                    penX = 0.0f;
                    penY += lineAdvance;
                    continue;
                }

                const auto codepoint = NormalizeRenderableCodepoint(rawCodepoint);

                float nextPenX = penX;
                float nextPenY = penY;
                stbtt_aligned_quad bakedQuad{};
                stbtt_GetBakedQuad(atlas.glyphs,
                                   atlas.width,
                                   atlas.height,
                                   static_cast<int>(codepoint) - kFirstBakedCodepoint,
                                   &nextPenX,
                                   &nextPenY,
                                   &bakedQuad,
                                   1);

                if (penY > rectHeight + lineAdvance)
                {
                    break;
                }

                const glm::vec2 min(textRun.rect.min.x + bakedQuad.x0, textRun.rect.max.y - bakedQuad.y1);
                const glm::vec2 max(textRun.rect.min.x + bakedQuad.x1, textRun.rect.max.y - bakedQuad.y0);
                if (max.x > textRun.rect.min.x && min.x < textRun.rect.max.x &&
                    max.y > textRun.rect.min.y && min.y < textRun.rect.max.y)
                {
                    glyphQuads.push_back(TextGlyphQuad{
                        .rect = UIRect{.min = min, .max = max},
                        .uvMin = glm::vec2(bakedQuad.s0, bakedQuad.t0),
                        .uvMax = glm::vec2(bakedQuad.s1, bakedQuad.t1),
                    });
                }

                penX = nextPenX;
                penY = nextPenY;
            }

            return glyphQuads;
        }

        void CollectUIQuads(scene::Entity *entity,
                            const scene::CanvasComponent *activeCanvas,
                            const glm::vec2 &viewportSize,
                            std::vector<UIQuad> &quads,
                            std::vector<UITextRun> &textRuns)
        {
            if (!entity || !entity->IsActive())
            {
                return;
            }

            if (auto *canvas = entity->GetComponent<scene::CanvasComponent>(); canvas && canvas->IsEnabled())
            {
                activeCanvas = canvas;
            }

            auto *rectTransform = entity->GetComponent<scene::RectTransformComponent>();
            auto *image = entity->GetComponent<scene::UIImageComponent>();
            auto *button = entity->GetComponent<scene::UIButtonComponent>();
            auto *text = entity->GetComponent<scene::UITextComponent>();
            if (activeCanvas && rectTransform && rectTransform->IsEnabled())
            {
                const float scaleFactor = std::max(activeCanvas->GetScaleFactor(), 0.0001f);
                auto rect = ResolveScreenRect(*rectTransform, viewportSize / scaleFactor);
                rect.min *= scaleFactor;
                rect.max *= scaleFactor;

                if ((image && image->IsEnabled()) || (button && button->IsEnabled()))
                {
                    glm::vec4 color = image && image->IsEnabled() ? image->GetColor() : glm::vec4(0.16f, 0.18f, 0.22f, 0.92f);
                    quads.push_back(UIQuad{
                        .rect = rect,
                        .color = ResolveButtonColor(button, color),
                        .sortingOrder = activeCanvas->GetSortingOrder(),
                        .entityId = entity->GetID(),
                    });
                }

                if (text && text->IsEnabled() && !text->GetText().empty())
                {
                    textRuns.push_back(UITextRun{
                        .rect = rect,
                        .text = text->GetText(),
                        .fontPath = text->GetFontPath(),
                        .color = text->GetColor(),
                        .fontSize = text->GetFontSize() * scaleFactor,
                        .sortingOrder = activeCanvas->GetSortingOrder(),
                        .entityId = entity->GetID(),
                    });
                }
            }

            for (auto *child : entity->GetChildren())
            {
                CollectUIQuads(child, activeCanvas, viewportSize, quads, textRuns);
            }
        }
    }

    void RuntimeUIPass::Initialize()
    {
        m_shader = CreateRuntimeUIShader();
        m_textShader = CreateRuntimeUITextShader();
        glGenVertexArrays(1, &m_vao);
    }

    void RuntimeUIPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.scene || !m_shader || !m_textShader || m_vao == 0)
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
            CollectUIQuads(rootEntity, nullptr, viewportSize, quads, textRuns);
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
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_shader->Bind();
        m_shader->SetUniform("uViewportSize", viewportSize);

        glBindVertexArray(m_vao);
        for (const auto &quad : quads)
        {
            m_shader->SetUniform("uRectMin", quad.rect.min);
            m_shader->SetUniform("uRectMax", quad.rect.max);
            m_shader->SetUniform("uColor", quad.color);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        std::stable_sort(textRuns.begin(), textRuns.end(),
                         [](const UITextRun &a, const UITextRun &b)
                         {
                             if (a.sortingOrder != b.sortingOrder)
                             {
                                 return a.sortingOrder < b.sortingOrder;
                             }

                             return a.entityId < b.entityId;
                         });

        m_textShader->Bind();
        m_textShader->SetUniform("uViewportSize", viewportSize);
        m_textShader->SetUniform("uFontAtlas", 0);

        glActiveTexture(GL_TEXTURE0);
        for (const auto &textRun : textRuns)
        {
            auto *fontAtlas = GetOrCreateFontAtlas(textRun.fontPath, textRun.fontSize);
            if (!fontAtlas || fontAtlas->textureId == 0)
            {
                continue;
            }

            glBindTexture(GL_TEXTURE_2D, fontAtlas->textureId);
            m_textShader->SetUniform("uColor", textRun.color);

            const auto glyphQuads = BuildTextGlyphQuads(textRun, *fontAtlas);
            for (const auto &glyphQuad : glyphQuads)
            {
                m_textShader->SetUniform("uRectMin", glyphQuad.rect.min);
                m_textShader->SetUniform("uRectMax", glyphQuad.rect.max);
                m_textShader->SetUniform("uUvMin", glyphQuad.uvMin);
                m_textShader->SetUniform("uUvMax", glyphQuad.uvMax);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}
