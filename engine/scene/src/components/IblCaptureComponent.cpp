#include "PlutoGE/scene/components/IblCaptureComponent.h"

#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr int kMinCaptureResolution = 32;
        constexpr int kMaxCaptureResolution = 2048;

        glm::vec3 ParseVec3(const std::string &value, const glm::vec3 &fallback)
        {
            glm::vec3 parsed = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f", &parsed.x, &parsed.y, &parsed.z);
            return parsed;
        }

        std::string SerializeVec3(const glm::vec3 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z);
        }

        std::string Base64Encode(const unsigned char *data, std::size_t size)
        {
            static constexpr char kAlphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            std::string encoded;
            encoded.reserve(((size + 2) / 3) * 4);
            for (std::size_t index = 0; index < size; index += 3)
            {
                const unsigned int octetA = data[index];
                const unsigned int octetB = index + 1 < size ? data[index + 1] : 0;
                const unsigned int octetC = index + 2 < size ? data[index + 2] : 0;
                const unsigned int triple = (octetA << 16) | (octetB << 8) | octetC;

                encoded.push_back(kAlphabet[(triple >> 18) & 0x3F]);
                encoded.push_back(kAlphabet[(triple >> 12) & 0x3F]);
                encoded.push_back(index + 1 < size ? kAlphabet[(triple >> 6) & 0x3F] : '=');
                encoded.push_back(index + 2 < size ? kAlphabet[triple & 0x3F] : '=');
            }

            return encoded;
        }

        int Base64DecodeValue(char character)
        {
            if (character >= 'A' && character <= 'Z')
                return character - 'A';
            if (character >= 'a' && character <= 'z')
                return character - 'a' + 26;
            if (character >= '0' && character <= '9')
                return character - '0' + 52;
            if (character == '+')
                return 62;
            if (character == '/')
                return 63;
            return -1;
        }

        std::vector<unsigned char> Base64Decode(const std::string &encoded)
        {
            std::vector<unsigned char> decoded;
            decoded.reserve((encoded.size() / 4) * 3);

            int values[4] = {};
            int valueCount = 0;
            for (const char character : encoded)
            {
                if (character == '=')
                {
                    values[valueCount++] = -2;
                }
                else
                {
                    const int value = Base64DecodeValue(character);
                    if (value < 0)
                    {
                        continue;
                    }
                    values[valueCount++] = value;
                }

                if (valueCount != 4)
                {
                    continue;
                }

                const int a = values[0] >= 0 ? values[0] : 0;
                const int b = values[1] >= 0 ? values[1] : 0;
                const int c = values[2] >= 0 ? values[2] : 0;
                const int d = values[3] >= 0 ? values[3] : 0;
                const unsigned int triple = (static_cast<unsigned int>(a) << 18) |
                                            (static_cast<unsigned int>(b) << 12) |
                                            (static_cast<unsigned int>(c) << 6) |
                                            static_cast<unsigned int>(d);

                decoded.push_back(static_cast<unsigned char>((triple >> 16) & 0xFF));
                if (values[2] != -2)
                {
                    decoded.push_back(static_cast<unsigned char>((triple >> 8) & 0xFF));
                }
                if (values[3] != -2)
                {
                    decoded.push_back(static_cast<unsigned char>(triple & 0xFF));
                }

                valueCount = 0;
            }

            return decoded;
        }

        void ExpandBounds(glm::vec3 &minBounds, glm::vec3 &maxBounds, const glm::vec3 &point)
        {
            minBounds = glm::min(minBounds, point);
            maxBounds = glm::max(maxBounds, point);
        }
    }

    void IblCaptureComponent::SetSize(const glm::vec3 &size)
    {
        m_size = glm::max(size, glm::vec3(0.0001f));
    }

    void IblCaptureComponent::SetIntensity(float intensity)
    {
        m_intensity = std::max(intensity, 0.0f);
    }

    void IblCaptureComponent::SetBlendDistance(float blendDistance)
    {
        m_blendDistance = std::max(blendDistance, 0.0f);
    }

    void IblCaptureComponent::SetResolution(int resolution)
    {
        const int clampedResolution = std::clamp(resolution, kMinCaptureResolution, kMaxCaptureResolution);
        if (m_resolution != clampedResolution)
        {
            m_resolution = clampedResolution;
            m_captureTexture.reset();
            m_capturePixels.clear();
            MarkDirty();
        }
    }

    void IblCaptureComponent::SetFarPlane(float farPlane)
    {
        const float clampedFarPlane = std::max(farPlane, 1.0f);
        if (m_farPlane != clampedFarPlane)
        {
            m_farPlane = clampedFarPlane;
            MarkDirty();
        }
    }

    render::Texture *IblCaptureComponent::EnsureCaptureTexture()
    {
        if (m_captureTexture && m_captureTexture->GetWidth() == m_resolution && m_captureTexture->GetHeight() == m_resolution)
        {
            return m_captureTexture.get();
        }

        m_captureTexture.reset(render::Texture::ColorCubemap(m_resolution, m_resolution));
        UploadStoredCapturePixels();
        return m_captureTexture.get();
    }

    void IblCaptureComponent::DiscardCaptureResult()
    {
        m_captureTexture.reset();
        m_capturePixels.clear();
        MarkDirty();
    }

    bool IblCaptureComponent::StoreCapturePixelsFromTexture()
    {
        if (!m_captureTexture || m_captureTexture->GetType() != GL_TEXTURE_CUBE_MAP || m_resolution <= 0)
        {
            m_capturePixels.clear();
            return false;
        }

        const std::size_t facePixelCount = static_cast<std::size_t>(m_resolution) *
                                           static_cast<std::size_t>(m_resolution) * 4;
        m_capturePixels.assign(facePixelCount * 6, 0.0f);

        glBindTexture(GL_TEXTURE_CUBE_MAP, m_captureTexture->GetTextureID());
        for (unsigned int face = 0; face < 6; ++face)
        {
            glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                          0,
                          GL_RGBA,
                          GL_FLOAT,
                          m_capturePixels.data() + facePixelCount * face);
        }
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        return true;
    }

    void IblCaptureComponent::UploadStoredCapturePixels()
    {
        if (!m_captureTexture || m_captureTexture->GetType() != GL_TEXTURE_CUBE_MAP || m_resolution <= 0)
        {
            return;
        }

        const std::size_t facePixelCount = static_cast<std::size_t>(m_resolution) *
                                           static_cast<std::size_t>(m_resolution) * 4;
        if (m_capturePixels.size() != facePixelCount * 6)
        {
            return;
        }

        glBindTexture(GL_TEXTURE_CUBE_MAP, m_captureTexture->GetTextureID());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (unsigned int face = 0; face < 6; ++face)
        {
            glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
                            0,
                            0,
                            0,
                            m_resolution,
                            m_resolution,
                            GL_RGBA,
                            GL_FLOAT,
                            m_capturePixels.data() + facePixelCount * face);
        }
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }

    glm::mat4 IblCaptureComponent::GetVolumeTransform() const
    {
        const auto *owner = GetOwner();
        const glm::mat4 ownerTransform = owner ? owner->GetWorldTransform() : glm::mat4(1.0f);
        return ownerTransform * glm::scale(glm::mat4(1.0f), m_size);
    }

    IblCaptureVolume IblCaptureComponent::BuildCaptureVolume() const
    {
        const glm::mat4 volumeTransform = GetVolumeTransform();
        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(-std::numeric_limits<float>::max());

        for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
        {
            const glm::vec3 localCorner(
                (cornerIndex & 1) ? 0.5f : -0.5f,
                (cornerIndex & 2) ? 0.5f : -0.5f,
                (cornerIndex & 4) ? 0.5f : -0.5f);
            ExpandBounds(minBounds, maxBounds, glm::vec3(volumeTransform * glm::vec4(localCorner, 1.0f)));
        }

        return IblCaptureVolume{
            .origin = minBounds,
            .size = glm::max(maxBounds - minBounds, glm::vec3(0.0001f)),
            .environmentMapTexture = m_captureTexture.get(),
            .intensity = m_intensity,
            .blendDistance = m_blendDistance,
        };
    }

    std::vector<Property> IblCaptureComponent::Serialize() const
    {
        auto properties = SerializeEditableProperties();
        properties.push_back({"Capture Pixels", PropertyType::String, m_capturePixels.empty()
                                                               ? std::string{}
                                                               : Base64Encode(reinterpret_cast<const unsigned char *>(m_capturePixels.data()),
                                                                              m_capturePixels.size() * sizeof(float))});
        return properties;
    }

    std::vector<Property> IblCaptureComponent::SerializeEditableProperties() const
    {
        return {
            {"Size", PropertyType::Vec3, SerializeVec3(m_size)},
            {"Intensity", PropertyType::Float, std::to_string(m_intensity)},
            {"Blend Distance", PropertyType::Float, std::to_string(m_blendDistance)},
            {"Resolution", PropertyType::Int, std::to_string(m_resolution)},
            {"Far Plane", PropertyType::Float, std::to_string(m_farPlane)},
            {"Dirty", PropertyType::Bool, m_dirty ? "true" : "false"},
        };
    }

    void IblCaptureComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Size")
            {
                SetSize(ParseVec3(property.value, m_size));
            }
            else if (property.name == "Intensity")
            {
                SetIntensity(std::stof(property.value));
            }
            else if (property.name == "Blend Distance")
            {
                SetBlendDistance(std::stof(property.value));
            }
            else if (property.name == "Resolution")
            {
                SetResolution(std::stoi(property.value));
            }
            else if (property.name == "Far Plane")
            {
                SetFarPlane(std::stof(property.value));
            }
            else if (property.name == "Dirty")
            {
                m_dirty = property.value == "true";
            }
            else if (property.name == "Capture Pixels")
            {
                const auto decoded = Base64Decode(property.value);
                const std::size_t facePixelCount = static_cast<std::size_t>(m_resolution) *
                                                   static_cast<std::size_t>(m_resolution) * 4;
                const std::size_t expectedByteCount = facePixelCount * 6 * sizeof(float);
                if (decoded.size() == expectedByteCount)
                {
                    m_capturePixels.resize(facePixelCount * 6);
                    std::memcpy(m_capturePixels.data(), decoded.data(), decoded.size());
                    EnsureCaptureTexture();
                    m_dirty = false;
                }
            }
        }
    }

    void IblCaptureComponent::Update(float deltaTime)
    {
        (void)deltaTime;

        if (!IsEnabled() || !m_captureTexture)
        {
            return;
        }

        auto *owner = GetOwner();
        auto *scene = owner ? owner->GetScene() : nullptr;
        if (!scene)
        {
            return;
        }

        scene->AddIblCaptureVolume(BuildCaptureVolume());
    }
}
