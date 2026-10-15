#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PlutoGE::assets
{
    // Small ordered JSON document model for editor and tool data files. Objects keep member
    // order and unknown members, so load-edit-save round trips only change what was edited.
    struct JsonValue
    {
        enum class Type { Null, Bool, Number, String, Array, Object };

        Type type = Type::Null;
        bool boolean = false;
        double number = 0.0;
        std::string text;
        std::vector<JsonValue> items;
        std::vector<std::pair<std::string, JsonValue>> members;

        static JsonValue MakeString(std::string value);
        static JsonValue MakeNumber(double value);
        static JsonValue MakeBool(bool value);
        static JsonValue MakeArray();
        static JsonValue MakeObject();

        bool IsObject() const { return type == Type::Object; }
        bool IsArray() const { return type == Type::Array; }
        const JsonValue *Find(std::string_view key) const;
        JsonValue *Find(std::string_view key);
        // Replaces an existing member in place, or appends a new one.
        JsonValue &Set(std::string_view key, JsonValue value);
        bool Remove(std::string_view key);

        std::string GetString(std::string_view key, std::string fallback = {}) const;
        double GetNumber(std::string_view key, double fallback = 0.0) const;
        bool GetBool(std::string_view key, bool fallback = false) const;
    };

    // Strict RFC 8259 parsing (no comments or trailing commas). On failure, error names the offset.
    bool ParseJson(std::string_view text, JsonValue &out, std::string *error = nullptr);
    // Indented output with a trailing newline; numbers use the shortest round-trip form.
    std::string WriteJson(const JsonValue &value, int indent = 2);
}
