#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    class Entity;

    // Selects the entities a camera renders by entity tag, similar to a culling
    // mask. Tags are inherited down the hierarchy, so tagging a weapon root
    // covers every mesh beneath it. Exclusion takes precedence over inclusion.
    class CameraTagFilter
    {
    public:
        CameraTagFilter() = default;
        CameraTagFilter(std::vector<std::string> includedTags, std::vector<std::string> excludedTags);

        // When non-empty, only entities carrying one of these tags are rendered.
        [[nodiscard]] const std::vector<std::string> &GetIncludedTags() const noexcept { return m_includedTags; }
        void SetIncludedTags(std::vector<std::string> tags);
        // Entities carrying one of these tags are never rendered.
        [[nodiscard]] const std::vector<std::string> &GetExcludedTags() const noexcept { return m_excludedTags; }
        void SetExcludedTags(std::vector<std::string> tags);

        // True when every entity is accepted, letting callers skip filtering.
        [[nodiscard]] bool AcceptsEverything() const noexcept { return m_includedTags.empty() && m_excludedTags.empty(); }
        // A null entity stands for untagged geometry, such as procedural draws.
        [[nodiscard]] bool Accepts(const Entity *entity) const;
        // Tests ignore tags independently of the include list, including parents.
        [[nodiscard]] bool Excludes(const Entity *entity) const;

        // Comma-separated tag lists, as edited in the Inspector and serialized.
        [[nodiscard]] static std::vector<std::string> ParseTagList(std::string_view text);
        [[nodiscard]] static std::string FormatTagList(const std::vector<std::string> &tags);

        bool operator==(const CameraTagFilter &) const = default;

    private:
        std::vector<std::string> m_includedTags;
        std::vector<std::string> m_excludedTags;
    };
}
