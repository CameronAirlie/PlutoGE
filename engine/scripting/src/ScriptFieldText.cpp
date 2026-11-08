// Text form of script field values. Every place that stores or transmits a field value (managed bridge, scene
// properties, scriptable object assets) goes through these functions so that they agree.

#include "PlutoGE/scripting/ScriptTypes.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <type_traits>

namespace PlutoGE::scripting
{
    namespace
    {
        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        template <typename T>
        std::string FormatNumber(T value)
        {
            std::array<char, 64> buffer{};
            const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            return std::string(buffer.data(), result.ptr);
        }

        template <typename T>
        T ParseNumber(std::string_view text, T fallback)
        {
            text = Trim(text);
            if (!text.empty() && text.front() == '+')
                text.remove_prefix(1);
            T value = fallback;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
            return result.ec == std::errc{} ? value : fallback;
        }

        // Comma-separated floats; components that are missing or malformed keep `fallback`'s value.
        template <int N>
        glm::vec<N, float> ParseVector(std::string_view text, glm::vec<N, float> fallback)
        {
            glm::vec<N, float> value = fallback;
            int index = 0;
            while (index < N)
            {
                const auto comma = text.find(',');
                value[index] = ParseNumber<float>(text.substr(0, comma), fallback[index]);
                ++index;
                if (comma == std::string_view::npos)
                    break;
                text.remove_prefix(comma + 1);
            }
            return value;
        }

        template <int N>
        std::string FormatVector(const glm::vec<N, float> &value)
        {
            std::string text;
            for (int index = 0; index < N; ++index)
            {
                if (index > 0)
                    text += ',';
                text += FormatNumber(value[index]);
            }
            return text;
        }
    }

    bool IsVector4FieldType(ScriptFieldType type)
    {
        return type == ScriptFieldType::Color || type == ScriptFieldType::Vector4 || type == ScriptFieldType::Quaternion;
    }

    std::string FormatFieldValueText(const ScriptFieldValue &value)
    {
        return std::visit(
            [](const auto &typedValue) -> std::string
            {
                using ValueType = std::decay_t<decltype(typedValue)>;
                if constexpr (std::is_same_v<ValueType, std::monostate>)
                    return {};
                else if constexpr (std::is_same_v<ValueType, bool>)
                    return typedValue ? "true" : "false";
                else if constexpr (std::is_same_v<ValueType, int32_t> || std::is_same_v<ValueType, uint32_t>)
                    return std::to_string(typedValue);
                else if constexpr (std::is_same_v<ValueType, float> || std::is_same_v<ValueType, double>)
                    return FormatNumber(typedValue);
                else if constexpr (std::is_same_v<ValueType, std::string>)
                    return typedValue;
                else if constexpr (std::is_same_v<ValueType, glm::vec2>)
                    return FormatVector<2>(typedValue);
                else if constexpr (std::is_same_v<ValueType, glm::vec3>)
                    return FormatVector<3>(typedValue);
                else if constexpr (std::is_same_v<ValueType, glm::vec4>)
                    return FormatVector<4>(typedValue);
                else if constexpr (std::is_same_v<ValueType, ScriptReferenceArray>)
                    return EncodeReferenceArray(typedValue);
                else
                    static_assert(sizeof(ValueType) == 0, "Every ScriptFieldValue alternative needs a text form");
            },
            value);
    }

    ScriptFieldValue ParseFieldValueText(ScriptFieldType type, std::string_view text)
    {
        switch (type)
        {
        case ScriptFieldType::Boolean:
        {
            const auto trimmed = Trim(text);
            return trimmed == "true" || trimmed == "1" || trimmed == "True";
        }
        case ScriptFieldType::Int32:
            return ParseNumber<int32_t>(text, 0);
        case ScriptFieldType::Float:
            return ParseNumber<float>(text, 0.0f);
        case ScriptFieldType::Double:
            return ParseNumber<double>(text, 0.0);
        case ScriptFieldType::Vector2:
            return ParseVector<2>(text, glm::vec2(0.0f));
        case ScriptFieldType::Vector3:
            return ParseVector<3>(text, glm::vec3(0.0f));
        case ScriptFieldType::Color:
        case ScriptFieldType::Vector4:
        case ScriptFieldType::Quaternion:
            return ParseVector<4>(text, std::get<glm::vec4>(MakeDefaultFieldValue(type)));
        case ScriptFieldType::None:
            return std::monostate{};
        default:
            break;
        }
        if (std::holds_alternative<uint32_t>(MakeDefaultFieldValue(type)))
            return ParseNumber<uint32_t>(text, 0u);
        // Strings, enums (by member name) and asset references.
        return std::string(text);
    }

    ScriptFieldType InferFieldType(const ScriptFieldValue &value)
    {
        if (std::holds_alternative<bool>(value))
            return ScriptFieldType::Boolean;
        if (std::holds_alternative<int32_t>(value))
            return ScriptFieldType::Int32;
        if (std::holds_alternative<float>(value))
            return ScriptFieldType::Float;
        if (std::holds_alternative<double>(value))
            return ScriptFieldType::Double;
        if (std::holds_alternative<std::string>(value))
            return ScriptFieldType::String;
        if (std::holds_alternative<glm::vec2>(value))
            return ScriptFieldType::Vector2;
        if (std::holds_alternative<glm::vec3>(value))
            return ScriptFieldType::Vector3;
        if (std::holds_alternative<glm::vec4>(value))
            return ScriptFieldType::Vector4;
        if (std::holds_alternative<uint32_t>(value))
            return ScriptFieldType::EntityId;
        return ScriptFieldType::None;
    }

    bool MigrateRenamedFields(const std::vector<ScriptFieldDefinition> &fields, std::unordered_map<std::string, ScriptFieldValue> &values)
    {
        bool moved = false;
        for (const auto &field : fields)
        {
            for (const auto &formerName : field.formerNames)
            {
                const auto former = values.find(formerName);
                if (former == values.end())
                    continue;
                if (!values.contains(field.name))
                    values.emplace(field.name, std::move(former->second));
                values.erase(formerName);
                moved = true;
            }
        }
        return moved;
    }
}
