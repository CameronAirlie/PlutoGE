#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    // Conservative lexical edits preserve comments, whitespace and external styles.
    // Unsupported/malformed markup is rejected; source editing remains available.
    class RmlSourceTools
    {
    public:
        struct Attribute { std::string name, value; std::size_t begin, end; };
        struct Element
        {
            std::string tag, id;
            std::size_t begin = 0, openEnd = 0, closeBegin = 0, end = 0, depth = 0;
            bool selfClosing = false;
            std::vector<Attribute> attributes;
        };
        static std::vector<Element> Parse(std::string_view source);
        static std::string SetAttribute(std::string source, std::size_t elementIndex, std::string_view name, std::string_view value);
        static std::string Insert(std::string source, std::size_t parentIndex, std::string_view tag, std::string_view id);
        static std::string Remove(std::string source, std::size_t elementIndex);
        static std::string SetText(std::string source, std::size_t elementIndex, std::string_view text);
        static std::string SetStyle(std::string source, std::size_t elementIndex, std::string_view property, std::string_view value);
        static std::string Decode(std::string_view value);
        static std::string Escape(std::string_view value);
    };
}
