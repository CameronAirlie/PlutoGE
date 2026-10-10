#include "PlutoGE/ui/RmlSourceTools.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <charconv>
#include <cstdint>

namespace PlutoGE::ui
{
    namespace
    {
        bool Name(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':'; }
        void Space(std::string_view source, std::size_t &i) { while (i < source.size() && std::isspace(static_cast<unsigned char>(source[i]))) ++i; }
        std::string ReadName(std::string_view source, std::size_t &i)
        {
            const auto start = i;
            while (i < source.size() && Name(source[i])) ++i;
            if (start == i) throw std::runtime_error("Expected an RML name");
            return std::string(source.substr(start, i - start));
        }
        void ValidName(std::string_view name)
        {
            if (name.empty() || !std::all_of(name.begin(), name.end(), Name)) throw std::invalid_argument("Invalid RML name");
        }
    }
    std::string RmlSourceTools::Escape(std::string_view value)
    {
        std::string result;
        for (char c : value)
            switch (c) { case '&': result += "&amp;"; break; case '<': result += "&lt;"; break; case '>': result += "&gt;"; break; case '"': result += "&quot;"; break; default: result += c; }
        return result;
    }
    std::string RmlSourceTools::Decode(std::string_view value)
    {
        std::string result;
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] != '&') { result += value[i]; continue; }
            const auto end = value.find(';', i + 1);
            if (end == std::string_view::npos) throw std::runtime_error("Unclosed attribute entity");
            auto entity = value.substr(i + 1, end - i - 1);
            if (entity == "amp") result += '&';
            else if (entity == "lt") result += '<';
            else if (entity == "gt") result += '>';
            else if (entity == "quot") result += '"';
            else if (entity == "apos") result += '\'';
            else if (entity.starts_with('#'))
            {
                entity.remove_prefix(1);
                int base = 10;
                if (entity.starts_with('x') || entity.starts_with('X')) { base = 16; entity.remove_prefix(1); }
                std::uint32_t code = 0;
                const auto parsed = std::from_chars(entity.data(), entity.data() + entity.size(), code, base);
                if (parsed.ec != std::errc{} || parsed.ptr != entity.data() + entity.size() || code == 0 || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
                    throw std::runtime_error("Invalid numeric attribute entity");
                if (code < 0x80) result += static_cast<char>(code);
                else
                {
                    if (code >= 0x10000) result += static_cast<char>(0xf0 | (code >> 18));
                    if (code >= 0x800) result += static_cast<char>((code >= 0x10000 ? 0x80 : 0xe0) | ((code >> 12) & 0x3f));
                    result += static_cast<char>((code >= 0x800 ? 0x80 : 0xc0) | ((code >> 6) & 0x3f));
                    result += static_cast<char>(0x80 | (code & 0x3f));
                }
            }
            else throw std::runtime_error("Use source editing for non-XML attribute entities");
            i = end;
        }
        return result;
    }
    std::vector<RmlSourceTools::Element> RmlSourceTools::Parse(std::string_view source)
    {
        std::vector<Element> result;
        std::vector<std::size_t> stack;
        std::size_t i = 0;
        while ((i = source.find('<', i)) != std::string_view::npos)
        {
            if (source.substr(i, 4) == "<!--")
            {
                const auto end = source.find("-->", i + 4);
                if (end == std::string_view::npos) throw std::runtime_error("Unclosed RML comment");
                i = end + 3; continue;
            }
            if (source.substr(i, 9) == "<![CDATA[")
            {
                const auto end = source.find("]]>", i + 9);
                if (end == std::string_view::npos) throw std::runtime_error("Unclosed CDATA");
                i = end + 3; continue;
            }
            if (source.substr(i, 2) == "<!" || source.substr(i, 2) == "<?") throw std::runtime_error("Declarations require source editing");
            const auto begin = i++;
            const bool closing = i < source.size() && source[i] == '/';
            if (closing) ++i;
            auto tag = ReadName(source, i);
            if (closing)
            {
                Space(source, i);
                if (i >= source.size() || source[i++] != '>' || stack.empty() || result[stack.back()].tag != tag)
                    throw std::runtime_error("Mismatched RML closing tag");
                auto &element = result[stack.back()];
                element.closeBegin = begin; element.end = i;
                stack.pop_back(); continue;
            }
            Element element{.tag = tag, .begin = begin, .depth = stack.size()};
            while (true)
            {
                Space(source, i);
                if (i >= source.size()) throw std::runtime_error("Unclosed RML tag");
                if (source[i] == '>' || source[i] == '/') break;
                const auto attributeBegin = i;
                auto name = ReadName(source, i);
                Space(source, i);
                if (i >= source.size() || source[i++] != '=') throw std::runtime_error("Visual editing requires quoted attribute values");
                Space(source, i);
                if (i >= source.size() || (source[i] != '"' && source[i] != '\'')) throw std::runtime_error("Visual editing requires quoted attribute values");
                const char quote = source[i++];
                const auto valueBegin = i;
                i = source.find(quote, i);
                if (i == std::string_view::npos) throw std::runtime_error("Unclosed RML attribute");
                auto value = Decode(source.substr(valueBegin, i - valueBegin));
                ++i;
                if (std::any_of(element.attributes.begin(), element.attributes.end(), [&](const auto &a) { return a.name == name; })) throw std::runtime_error("Duplicate RML attribute");
                if (name == "id") element.id = value;
                element.attributes.push_back({std::move(name), std::move(value), attributeBegin, i});
            }
            element.selfClosing = source[i] == '/';
            if (element.selfClosing) ++i;
            if (i >= source.size() || source[i++] != '>') throw std::runtime_error("Malformed RML tag");
            element.openEnd = i;
            if (element.selfClosing) { element.closeBegin = i; element.end = i; }
            result.push_back(std::move(element));
            const auto index = result.size() - 1;
            if (!result[index].selfClosing)
            {
                stack.push_back(index);
                // CSS and script text may contain '<'; skip to their closing tag.
                if (tag == "style" || tag == "script")
                {
                    i = source.find("</" + tag, i);
                    if (i == std::string_view::npos) throw std::runtime_error("Unclosed raw-text element");
                }
            }
        }
        if (!stack.empty()) throw std::runtime_error("Unclosed RML element");
        for (std::size_t a = 0; a < result.size(); ++a)
            if (!result[a].id.empty())
                for (std::size_t b = a + 1; b < result.size(); ++b)
                    if (result[a].id == result[b].id) throw std::runtime_error("Duplicate RML element ID");
        return result;
    }
    std::string RmlSourceTools::SetAttribute(std::string source, std::size_t index, std::string_view name, std::string_view value)
    {
        ValidName(name);
        const auto elements = Parse(source);
        const auto &element = elements.at(index);
        if (name == "id" && !value.empty())
            for (std::size_t i = 0; i < elements.size(); ++i)
                if (i != index && elements[i].id == value) throw std::invalid_argument("Duplicate element ID");
        const auto replacement = std::string(name) + "=\"" + Escape(value) + "\"";
        const auto attribute = std::find_if(element.attributes.begin(), element.attributes.end(), [&](const auto &a) { return a.name == name; });
        if (attribute != element.attributes.end()) source.replace(attribute->begin, attribute->end - attribute->begin, replacement);
        else source.insert(element.openEnd - (element.selfClosing ? 2 : 1), " " + replacement);
        return source;
    }
    std::string RmlSourceTools::Insert(std::string source, std::size_t index, std::string_view tag, std::string_view id)
    {
        ValidName(tag);
        ValidName(id);
        const auto elements = Parse(source);
        const auto &parent = elements.at(index);
        for (const auto &element : elements) if (element.id == id) throw std::invalid_argument("Duplicate element ID");
        if (parent.tag == "head" || parent.tag == "style" || parent.tag == "script") throw std::invalid_argument("Select a layout element");
        std::string child = "\n<" + std::string(tag) + " id=\"" + Escape(id) + "\"";
        if (tag == "input") child += " type=\"text\"";
        child += " style=\"display: block; width: 200dp; height: 40dp;\">";
        if (tag == "button" || tag == "label") child += "Label";
        child += "</" + std::string(tag) + ">\n";
        if (parent.selfClosing) source.replace(parent.openEnd - 2, 2, ">" + child + "</" + parent.tag + ">");
        else source.insert(parent.closeBegin, child);
        return source;
    }
    std::string RmlSourceTools::Remove(std::string source, std::size_t index)
    {
        const auto element = Parse(source).at(index);
        if (element.tag == "rml" || element.tag == "head" || element.tag == "body") throw std::invalid_argument("Cannot remove document roots");
        source.erase(element.begin, element.end - element.begin);
        return source;
    }
    std::string RmlSourceTools::SetText(std::string source, std::size_t index, std::string_view text)
    {
        const auto elements = Parse(source);
        const auto &element = elements.at(index);
        if (element.tag == "rml" || element.tag == "head" || element.tag == "style" || element.tag == "script") throw std::invalid_argument("Select a text widget");
        for (const auto &child : elements)
            if (child.begin > element.begin && child.begin < element.end) throw std::invalid_argument("Text editing cannot replace child widgets");
        if (element.selfClosing) source.replace(element.openEnd - 2, 2, ">" + Escape(text) + "</" + element.tag + ">");
        else source.replace(element.openEnd, element.closeBegin - element.openEnd, Escape(text));
        return source;
    }
    std::string RmlSourceTools::SetStyle(std::string source, std::size_t index, std::string_view property, std::string_view value)
    {
        ValidName(property);
        if (value.find_first_of(";{}") != std::string_view::npos) throw std::invalid_argument("Enter one style value");
        const auto elements = Parse(source);
        const auto &element = elements.at(index);
        std::string style;
        for (const auto &attribute : element.attributes) if (attribute.name == "style") style = attribute.value;
        // Append a local override without interpreting/reformatting authored CSS.
        // The engine's RCSS cascade resolves the last declaration.
        if (!style.empty() && style.back() != ';') style += ';';
        style += " " + std::string(property) + ": " + std::string(value) + ";";
        return SetAttribute(std::move(source), index, "style", style);
    }
}
