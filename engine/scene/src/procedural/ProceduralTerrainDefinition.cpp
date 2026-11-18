#include "PlutoGE/scene/procedural/ProceduralTerrainDefinition.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>

namespace PlutoGE::scene::procedural
{
    namespace
    {
        std::string Escape(std::string_view text)
        {
            static constexpr char Hex[] = "0123456789ABCDEF";
            std::string out;
            out.reserve(text.size());
            for (const char character : text)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (byte <= ' ' || character == '%' || character == '=' || character == ',' || byte == 0x7f)
                {
                    out.push_back('%');
                    out.push_back(Hex[byte >> 4]);
                    out.push_back(Hex[byte & 15]);
                }
                else
                    out.push_back(character);
            }
            return out;
        }

        std::string Unescape(std::string_view text)
        {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            std::string out;
            out.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '%' && index + 2 < text.size() && hex(text[index + 1]) >= 0 && hex(text[index + 2]) >= 0)
                {
                    out.push_back(static_cast<char>(hex(text[index + 1]) * 16 + hex(text[index + 2])));
                    index += 2;
                }
                else
                    out.push_back(text[index]);
            }
            return out;
        }

        std::string Number(float value)
        {
            char buffer[32];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            return std::string(buffer, result.ptr);
        }

        std::string Number(std::uint32_t value) { return std::to_string(value); }

        std::string Pair(glm::vec2 value) { return Number(value.x) + "," + Number(value.y); }

        // One record being written: "KIND key=value key=value".
        class Record
        {
        public:
            explicit Record(std::string &out, std::string_view kind) : m_out(out) { m_out.append(kind); }
            ~Record() { m_out.push_back('\n'); }
            Record &Field(std::string_view key, std::string_view value)
            {
                m_out.push_back(' ');
                m_out.append(key);
                m_out.push_back('=');
                m_out.append(value);
                return *this;
            }
            Record &Text(std::string_view key, std::string_view value) { return Field(key, Escape(value)); }

        private:
            std::string &m_out;
        };

        void WriteRules(std::string &out, const TerrainRuleStack &stack)
        {
            for (const auto &rule : stack.rules)
            {
                const auto &noise = rule.noise;
                Record(out, "RULE")
                    .Field("enabled", rule.enabled ? "1" : "0")
                    .Field("input", ToString(rule.input))
                    .Field("constant", Number(rule.constant))
                    .Field("remap", ToString(rule.remap))
                    .Field("min", Number(rule.min))
                    .Field("max", Number(rule.max))
                    .Field("falloff", Number(rule.falloff))
                    .Field("invert", rule.invert ? "1" : "0")
                    .Text("source", rule.source)
                    .Field("noise", std::string(ToString(noise.type)) + "," + Number(noise.scale) + "," + std::to_string(noise.octaves) + "," +
                                        Number(noise.lacunarity) + "," + Number(noise.gain) + "," + Number(noise.seed))
                    .Field("blend", ToString(rule.blend))
                    .Field("opacity", Number(rule.opacity));
            }
        }

        // ---- reading -------------------------------------------------------------------------------------------
        using Fields = std::map<std::string, std::string, std::less<>>;

        std::vector<std::string_view> Split(std::string_view text, char separator)
        {
            std::vector<std::string_view> parts;
            std::size_t begin = 0;
            while (begin <= text.size())
            {
                const auto end = text.find(separator, begin);
                parts.push_back(text.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
                if (end == std::string_view::npos)
                    break;
                begin = end + 1;
            }
            return parts;
        }

        template <typename T>
        void Read(const Fields &fields, std::string_view key, T &value)
        {
            const auto found = fields.find(key);
            if (found == fields.end())
                return;
            T parsed{};
            const auto &text = found->second;
            if (std::from_chars(text.data(), text.data() + text.size(), parsed).ec == std::errc())
                value = parsed;
        }

        void ReadBool(const Fields &fields, std::string_view key, bool &value)
        {
            if (const auto found = fields.find(key); found != fields.end())
                value = found->second == "1" || found->second == "true";
        }

        void ReadText(const Fields &fields, std::string_view key, std::string &value)
        {
            if (const auto found = fields.find(key); found != fields.end())
                value = Unescape(found->second);
        }

        void ReadPair(const Fields &fields, std::string_view key, glm::vec2 &value)
        {
            const auto found = fields.find(key);
            if (found == fields.end())
                return;
            const auto parts = Split(found->second, ',');
            glm::vec2 parsed = value;
            if (parts.size() == 2 &&
                std::from_chars(parts[0].data(), parts[0].data() + parts[0].size(), parsed.x).ec == std::errc() &&
                std::from_chars(parts[1].data(), parts[1].data() + parts[1].size(), parsed.y).ec == std::errc())
                value = parsed;
        }

        template <typename Enum>
        void ReadEnum(const Fields &fields, std::string_view key, Enum &value, std::optional<Enum> (*parse)(std::string_view))
        {
            if (const auto found = fields.find(key); found != fields.end())
                if (const auto parsed = parse(found->second))
                    value = *parsed;
        }

        TerrainRule ReadRule(const Fields &fields)
        {
            TerrainRule rule;
            ReadBool(fields, "enabled", rule.enabled);
            ReadEnum(fields, "input", rule.input, &ParseRuleInput);
            Read(fields, "constant", rule.constant);
            ReadEnum(fields, "remap", rule.remap, &ParseRuleRemap);
            Read(fields, "min", rule.min);
            Read(fields, "max", rule.max);
            Read(fields, "falloff", rule.falloff);
            ReadBool(fields, "invert", rule.invert);
            ReadText(fields, "source", rule.source);
            ReadEnum(fields, "blend", rule.blend, &ParseRuleBlend);
            Read(fields, "opacity", rule.opacity);
            if (const auto noise = fields.find("noise"); noise != fields.end())
            {
                const auto parts = Split(noise->second, ',');
                if (parts.size() == 6)
                {
                    if (const auto type = ParseNoiseType(parts[0]))
                        rule.noise.type = *type;
                    const auto number = [](std::string_view text, auto &target) { std::from_chars(text.data(), text.data() + text.size(), target); };
                    number(parts[1], rule.noise.scale);
                    number(parts[2], rule.noise.octaves);
                    number(parts[3], rule.noise.lacunarity);
                    number(parts[4], rule.noise.gain);
                    number(parts[5], rule.noise.seed);
                }
            }
            return rule;
        }
    }

    std::string ProceduralTerrainDefinition::Encode() const
    {
        std::string out;
        Record(out, "PROCEDURAL").Field("version", std::to_string(Version)).Field("seed", Number(seed)).Field("blend", Number(biomeBlend));
        for (const auto &mask : masks)
            Record(out, "MASK")
                .Text("name", mask.name)
                .Text("image", mask.image)
                .Field("channel", std::to_string(mask.channel))
                .Field("min", Pair(mask.worldMin))
                .Field("size", Pair(mask.worldSize));
        if (caves != volume::CaveNetworkSettings{})
            Record(out, "CAVES")
                .Field("enabled", caves.enabled ? "1" : "0")
                .Field("seed", Number(caves.seed))
                .Field("cell", Number(caves.cellSize))
                .Field("tunnels", std::to_string(caves.tunnelsPerCell))
                .Field("length", Number(caves.tunnelLength))
                .Field("step", Number(caves.stepLength))
                .Field("radius", Pair(caves.radius))
                .Field("turn", Number(caves.turnRate))
                .Field("slope", Number(caves.maxSlope))
                .Field("altitude", Pair(caves.altitude))
                .Field("chambers", Number(caves.chamberChance))
                .Field("chamber", Number(caves.chamberRadius))
                .Field("wobble", Number(caves.wobble))
                .Field("wobbleScale", Number(caves.wobbleScale));
        for (const auto &biome : biomes)
        {
            Record(out, "BIOME").Text("name", biome.name).Field("enabled", biome.enabled ? "1" : "0").Text("mask", biome.mask);
            WriteRules(out, biome.weight);
            for (const auto &layer : biome.layers)
            {
                Record(out, "LAYER").Text("name", layer.layer);
                WriteRules(out, layer.coverage);
            }
            for (const auto &scatter : biome.scatter)
            {
                Record(out, "SCATTER")
                    .Text("type", scatter.foliageType)
                    .Field("spacing", Number(scatter.spacing))
                    .Field("footprint", Number(scatter.footprint))
                    .Field("density", Number(scatter.densityScale))
                    .Field("slope", Pair(scatter.slopeLimits))
                    .Field("height", Pair(scatter.heightLimits))
                    .Field("scale", Pair(scatter.scaleRange))
                    .Field("align", Number(scatter.alignToSlope))
                    .Field("sink", Number(scatter.sink))
                    .Field("yaw", scatter.randomYaw ? "1" : "0")
                    .Field("seed", Number(scatter.seed));
                WriteRules(out, scatter.density);
            }
        }
        return out;
    }

    std::optional<ProceduralTerrainDefinition> ProceduralTerrainDefinition::Decode(std::string_view text, std::string *error)
    {
        ProceduralTerrainDefinition definition;
        if (text.empty())
            return definition;
        bool sawHeader = false;
        TerrainRuleStack *rules = nullptr;
        for (auto line : Split(text, '\n'))
        {
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            if (line.empty())
                continue;
            const auto tokens = Split(line, ' ');
            const auto kind = tokens.front();
            Fields fields;
            for (std::size_t index = 1; index < tokens.size(); ++index)
            {
                const auto equals = tokens[index].find('=');
                if (equals != std::string_view::npos)
                    fields.emplace(std::string(tokens[index].substr(0, equals)), std::string(tokens[index].substr(equals + 1)));
            }

            if (kind == "PROCEDURAL")
            {
                int version = 0;
                Read(fields, "version", version);
                if (version < 1 || version > Version)
                {
                    if (error)
                        *error = "Unsupported procedural terrain version " + std::to_string(version) + ".";
                    return std::nullopt;
                }
                Read(fields, "seed", definition.seed);
                Read(fields, "blend", definition.biomeBlend);
                sawHeader = true;
            }
            else if (!sawHeader)
            {
                if (error)
                    *error = "Not a procedural terrain definition.";
                return std::nullopt;
            }
            else if (kind == "CAVES")
            {
                auto &caves = definition.caves;
                ReadBool(fields, "enabled", caves.enabled);
                Read(fields, "seed", caves.seed);
                Read(fields, "cell", caves.cellSize);
                Read(fields, "tunnels", caves.tunnelsPerCell);
                Read(fields, "length", caves.tunnelLength);
                Read(fields, "step", caves.stepLength);
                ReadPair(fields, "radius", caves.radius);
                Read(fields, "turn", caves.turnRate);
                Read(fields, "slope", caves.maxSlope);
                ReadPair(fields, "altitude", caves.altitude);
                Read(fields, "chambers", caves.chamberChance);
                Read(fields, "chamber", caves.chamberRadius);
                Read(fields, "wobble", caves.wobble);
                Read(fields, "wobbleScale", caves.wobbleScale);
                caves.cellSize = std::max(caves.cellSize, 1.0f);
                caves.tunnelsPerCell = std::clamp(caves.tunnelsPerCell, 0, 64);
                caves.stepLength = std::max(caves.stepLength, 0.5f);
                rules = nullptr;
            }
            else if (kind == "MASK")
            {
                auto &mask = definition.masks.emplace_back();
                ReadText(fields, "name", mask.name);
                ReadText(fields, "image", mask.image);
                Read(fields, "channel", mask.channel);
                mask.channel = std::clamp(mask.channel, 0, 3);
                ReadPair(fields, "min", mask.worldMin);
                ReadPair(fields, "size", mask.worldSize);
                rules = nullptr;
            }
            else if (kind == "BIOME")
            {
                auto &biome = definition.biomes.emplace_back();
                ReadText(fields, "name", biome.name);
                ReadBool(fields, "enabled", biome.enabled);
                ReadText(fields, "mask", biome.mask);
                rules = &biome.weight;
            }
            else if (kind == "LAYER" && !definition.biomes.empty())
            {
                auto &layer = definition.biomes.back().layers.emplace_back();
                ReadText(fields, "name", layer.layer);
                rules = &layer.coverage;
            }
            else if (kind == "SCATTER" && !definition.biomes.empty())
            {
                auto &scatter = definition.biomes.back().scatter.emplace_back();
                ReadText(fields, "type", scatter.foliageType);
                Read(fields, "spacing", scatter.spacing);
                Read(fields, "footprint", scatter.footprint);
                Read(fields, "density", scatter.densityScale);
                ReadPair(fields, "slope", scatter.slopeLimits);
                ReadPair(fields, "height", scatter.heightLimits);
                ReadPair(fields, "scale", scatter.scaleRange);
                Read(fields, "align", scatter.alignToSlope);
                Read(fields, "sink", scatter.sink);
                ReadBool(fields, "yaw", scatter.randomYaw);
                Read(fields, "seed", scatter.seed);
                rules = &scatter.density;
            }
            else if (kind == "RULE" && rules)
                rules->rules.push_back(ReadRule(fields));
            // Anything else comes from a newer writer and is skipped.
        }
        if (!sawHeader)
        {
            if (error)
                *error = "Not a procedural terrain definition.";
            return std::nullopt;
        }
        return definition;
    }

    std::vector<std::string> ProceduralTerrainDefinition::ReferencedLayers() const
    {
        std::set<std::string> names;
        for (const auto &biome : biomes)
            for (const auto &layer : biome.layers)
                names.insert(layer.layer);
        return {names.begin(), names.end()};
    }

    std::vector<std::string> ProceduralTerrainDefinition::ReferencedFoliageTypes() const
    {
        std::set<std::string> names;
        for (const auto &biome : biomes)
            for (const auto &scatter : biome.scatter)
                names.insert(scatter.foliageType);
        return {names.begin(), names.end()};
    }
}
