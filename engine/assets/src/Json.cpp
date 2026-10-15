#include "PlutoGE/assets/Json.h"

#include <charconv>
#include <cmath>
#include <cstdint>

namespace PlutoGE::assets
{
    JsonValue JsonValue::MakeString(std::string value) { JsonValue v; v.type = Type::String; v.text = std::move(value); return v; }
    JsonValue JsonValue::MakeNumber(double value) { JsonValue v; v.type = Type::Number; v.number = value; return v; }
    JsonValue JsonValue::MakeBool(bool value) { JsonValue v; v.type = Type::Bool; v.boolean = value; return v; }
    JsonValue JsonValue::MakeArray() { JsonValue v; v.type = Type::Array; return v; }
    JsonValue JsonValue::MakeObject() { JsonValue v; v.type = Type::Object; return v; }

    const JsonValue *JsonValue::Find(std::string_view key) const
    {
        for (const auto &[name, value] : members)
            if (name == key) return &value;
        return nullptr;
    }

    JsonValue *JsonValue::Find(std::string_view key)
    {
        for (auto &[name, value] : members)
            if (name == key) return &value;
        return nullptr;
    }

    JsonValue &JsonValue::Set(std::string_view key, JsonValue value)
    {
        type = Type::Object;
        if (auto *existing = Find(key)) return *existing = std::move(value);
        members.emplace_back(std::string(key), std::move(value));
        return members.back().second;
    }

    bool JsonValue::Remove(std::string_view key)
    {
        for (auto it = members.begin(); it != members.end(); ++it)
            if (it->first == key) { members.erase(it); return true; }
        return false;
    }

    std::string JsonValue::GetString(std::string_view key, std::string fallback) const
    {
        const auto *value = Find(key);
        return value && value->type == Type::String ? value->text : fallback;
    }

    double JsonValue::GetNumber(std::string_view key, double fallback) const
    {
        const auto *value = Find(key);
        return value && value->type == Type::Number ? value->number : fallback;
    }

    bool JsonValue::GetBool(std::string_view key, bool fallback) const
    {
        const auto *value = Find(key);
        return value && value->type == Type::Bool ? value->boolean : fallback;
    }

    namespace
    {
        struct Parser
        {
            std::string_view text;
            std::size_t at = 0;
            std::string error;
            int depth = 0;

            bool Fail(const char *message)
            {
                if (error.empty()) error = std::string(message) + " at offset " + std::to_string(at);
                return false;
            }
            void Skip()
            {
                while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\n' || text[at] == '\r')) ++at;
            }
            bool Literal(std::string_view word)
            {
                if (text.substr(at, word.size()) != word) return Fail("Invalid literal");
                at += word.size();
                return true;
            }
            static void AppendUtf8(std::string &out, std::uint32_t code)
            {
                if (code < 0x80) out += static_cast<char>(code);
                else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
                else if (code < 0x10000)
                {
                    out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F));
                }
            }
            bool Hex4(std::uint32_t &code)
            {
                if (at + 4 > text.size()) return Fail("Truncated escape");
                code = 0;
                for (int i = 0; i < 4; ++i)
                {
                    const char c = text[at++];
                    code <<= 4;
                    if (c >= '0' && c <= '9') code |= static_cast<std::uint32_t>(c - '0');
                    else if (c >= 'a' && c <= 'f') code |= static_cast<std::uint32_t>(c - 'a' + 10);
                    else if (c >= 'A' && c <= 'F') code |= static_cast<std::uint32_t>(c - 'A' + 10);
                    else return Fail("Invalid unicode escape");
                }
                return true;
            }
            bool String(std::string &out)
            {
                ++at; // opening quote
                while (at < text.size())
                {
                    const char c = text[at++];
                    if (c == '"') return true;
                    if (static_cast<unsigned char>(c) < 0x20) return Fail("Control character in string");
                    if (c != '\\') { out += c; continue; }
                    if (at >= text.size()) break;
                    switch (const char e = text[at++])
                    {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        std::uint32_t code = 0;
                        if (!Hex4(code)) return false;
                        if (code >= 0xD800 && code <= 0xDBFF)
                        {
                            std::uint32_t low = 0;
                            if (text.substr(at, 2) != "\\u") return Fail("Unpaired surrogate");
                            at += 2;
                            if (!Hex4(low) || low < 0xDC00 || low > 0xDFFF) return Fail("Invalid surrogate pair");
                            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        }
                        AppendUtf8(out, code);
                        break;
                    }
                    default:
                        (void)e;
                        return Fail("Invalid escape");
                    }
                }
                return Fail("Unterminated string");
            }
            bool Number(double &out)
            {
                const auto start = at;
                if (at < text.size() && text[at] == '-') ++at;
                while (at < text.size() && ((text[at] >= '0' && text[at] <= '9') || text[at] == '.' || text[at] == 'e' ||
                                            text[at] == 'E' || text[at] == '+' || text[at] == '-'))
                    ++at;
                const auto token = text.substr(start, at - start);
                const std::size_t first = !token.empty() && token[0] == '-' ? 1 : 0;
                if (token.size() <= first || token[first] < '0' || token[first] > '9' ||
                    (token[first] == '0' && token.size() > first + 1 && token[first + 1] >= '0' && token[first + 1] <= '9'))
                    return Fail("Invalid number"); // JSON requires a leading digit and forbids leading zeros.
                const auto result = std::from_chars(token.data(), token.data() + token.size(), out);
                if (result.ec != std::errc{} || result.ptr != token.data() + token.size())
                    return Fail("Invalid number");
                return std::isfinite(out) || Fail("Number out of range");
            }
            bool Value(JsonValue &out)
            {
                if (++depth > 256) return Fail("Nesting too deep");
                Skip();
                if (at >= text.size()) return Fail("Unexpected end");
                bool ok = true;
                switch (text[at])
                {
                case '{':
                {
                    out = JsonValue::MakeObject();
                    ++at; Skip();
                    if (at < text.size() && text[at] == '}') { ++at; break; }
                    while (ok)
                    {
                        Skip();
                        if (at >= text.size() || text[at] != '"') { ok = Fail("Expected member name"); break; }
                        std::string key;
                        if (!String(key)) { ok = false; break; }
                        Skip();
                        if (at >= text.size() || text[at] != ':') { ok = Fail("Expected ':'"); break; }
                        ++at;
                        JsonValue member;
                        if (!Value(member)) { ok = false; break; }
                        out.members.emplace_back(std::move(key), std::move(member));
                        Skip();
                        if (at < text.size() && text[at] == ',') { ++at; continue; }
                        if (at < text.size() && text[at] == '}') { ++at; break; }
                        ok = Fail("Expected ',' or '}'");
                    }
                    break;
                }
                case '[':
                {
                    out = JsonValue::MakeArray();
                    ++at; Skip();
                    if (at < text.size() && text[at] == ']') { ++at; break; }
                    while (ok)
                    {
                        JsonValue item;
                        if (!Value(item)) { ok = false; break; }
                        out.items.push_back(std::move(item));
                        Skip();
                        if (at < text.size() && text[at] == ',') { ++at; continue; }
                        if (at < text.size() && text[at] == ']') { ++at; break; }
                        ok = Fail("Expected ',' or ']'");
                    }
                    break;
                }
                case '"': out = JsonValue::MakeString({}); ok = String(out.text); break;
                case 't': out = JsonValue::MakeBool(true); ok = Literal("true"); break;
                case 'f': out = JsonValue::MakeBool(false); ok = Literal("false"); break;
                case 'n': out = JsonValue{}; ok = Literal("null"); break;
                default: out = JsonValue::MakeNumber(0); ok = Number(out.number); break;
                }
                --depth;
                return ok;
            }
        };

        void WriteString(std::string &out, std::string_view value)
        {
            out += '"';
            for (const char c : value)
            {
                switch (c)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        constexpr char digits[] = "0123456789abcdef";
                        out += "\\u00";
                        out += digits[(c >> 4) & 0xF];
                        out += digits[c & 0xF];
                    }
                    else out += c;
                }
            }
            out += '"';
        }

        void Write(std::string &out, const JsonValue &value, int indent, int level)
        {
            const auto newline = [&](int depth) {
                if (indent <= 0) return;
                out += '\n';
                out.append(static_cast<std::size_t>(indent * depth), ' ');
            };
            switch (value.type)
            {
            case JsonValue::Type::Null: out += "null"; break;
            case JsonValue::Type::Bool: out += value.boolean ? "true" : "false"; break;
            case JsonValue::Type::Number:
            {
                if (!std::isfinite(value.number)) { out += "null"; break; }
                char buffer[32];
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.number);
                out.append(buffer, result.ptr);
                break;
            }
            case JsonValue::Type::String: WriteString(out, value.text); break;
            case JsonValue::Type::Array:
                if (value.items.empty()) { out += "[]"; break; }
                out += '[';
                for (std::size_t i = 0; i < value.items.size(); ++i)
                {
                    newline(level + 1);
                    Write(out, value.items[i], indent, level + 1);
                    if (i + 1 < value.items.size()) out += ',';
                }
                newline(level);
                out += ']';
                break;
            case JsonValue::Type::Object:
                if (value.members.empty()) { out += "{}"; break; }
                out += '{';
                for (std::size_t i = 0; i < value.members.size(); ++i)
                {
                    newline(level + 1);
                    WriteString(out, value.members[i].first);
                    out += indent > 0 ? ": " : ":";
                    Write(out, value.members[i].second, indent, level + 1);
                    if (i + 1 < value.members.size()) out += ',';
                }
                newline(level);
                out += '}';
                break;
            }
        }
    }

    bool ParseJson(std::string_view text, JsonValue &out, std::string *error)
    {
        Parser parser{text};
        if (text.substr(0, 3) == "\xEF\xBB\xBF") parser.at = 3; // UTF-8 BOM
        JsonValue value;
        bool ok = parser.Value(value);
        if (ok)
        {
            parser.Skip();
            if (parser.at != text.size()) ok = parser.Fail("Trailing characters");
        }
        if (!ok)
        {
            if (error) *error = parser.error;
            return false;
        }
        out = std::move(value);
        return true;
    }

    std::string WriteJson(const JsonValue &value, int indent)
    {
        std::string out;
        Write(out, value, indent, 0);
        out += '\n';
        return out;
    }
}
