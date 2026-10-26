#pragma once

#include "PlutoGE/scene/components/Component.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Exception-free property encoding shared by the native UI components.
// Malformed values fall back to the current value instead of throwing, so a
// hand-edited scene cannot abort loading.
namespace PlutoGE::scene::uiprops
{
    inline std::string FormatFloat(float value)
    {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        return std::string(buffer, result.ptr);
    }

    inline bool ParseFloat(std::string_view text, float &value)
    {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{};
    }

    inline float ReadFloat(std::string_view text, float fallback)
    {
        float value = fallback;
        return ParseFloat(text, value) ? value : fallback;
    }

    inline int ReadInt(std::string_view text, int fallback)
    {
        int value = fallback;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} ? value : fallback;
    }

    inline std::uint32_t ReadEntity(std::string_view text, std::uint32_t fallback = 0)
    {
        std::uint32_t value = fallback;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} ? value : fallback;
    }

    inline Property EntityProperty(std::string name, std::uint32_t entity)
    {
        return {std::move(name), PropertyType::Entity, std::to_string(entity)};
    }

    inline bool ReadBool(std::string_view text) { return text == "true" || text == "1"; }
    inline std::string FormatBool(bool value) { return value ? "true" : "false"; }

    template <int N>
    std::string FormatVector(const glm::vec<N, float> &value)
    {
        std::string text;
        for (int index = 0; index < N; ++index)
        {
            if (index) text += ',';
            text += FormatFloat(value[index]);
        }
        return text;
    }

    template <int N>
    glm::vec<N, float> ReadVector(std::string_view text, const glm::vec<N, float> &fallback)
    {
        glm::vec<N, float> value = fallback;
        for (int index = 0; index < N; ++index)
        {
            const auto comma = text.find(',');
            if (!ParseFloat(text.substr(0, comma), value[index]))
                return fallback;
            if (comma == std::string_view::npos)
                return index == N - 1 ? value : fallback;
            text.remove_prefix(comma + 1);
        }
        return value;
    }

    template <typename E>
    Property EnumProperty(std::string name, E value, std::vector<std::string> options)
    {
        return {std::move(name), PropertyType::Enum, std::to_string(static_cast<int>(value)), std::move(options)};
    }

    template <typename E>
    E ReadEnum(std::string_view text, E last, E fallback)
    {
        const int value = ReadInt(text, static_cast<int>(fallback));
        return static_cast<E>(std::clamp(value, 0, static_cast<int>(last)));
    }

    inline Property FloatProperty(std::string name, float value) { return {std::move(name), PropertyType::Float, FormatFloat(value)}; }
    inline Property IntProperty(std::string name, int value) { return {std::move(name), PropertyType::Int, std::to_string(value)}; }
    inline Property BoolProperty(std::string name, bool value) { return {std::move(name), PropertyType::Bool, FormatBool(value)}; }
    inline Property Vec2Property(std::string name, glm::vec2 value) { return {std::move(name), PropertyType::Vec2, FormatVector(value)}; }
    inline Property ColorProperty(std::string name, glm::vec4 value) { return {std::move(name), PropertyType::Color, FormatVector(value)}; }
    // Four-component values that are not colours (padding, corner radii).
    inline Property Vec4Property(std::string name, glm::vec4 value) { return {std::move(name), PropertyType::Vec4, FormatVector(value)}; }
}
