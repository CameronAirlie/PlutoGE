#pragma once
#include <cstdint>

namespace PlutoGE::render
{
    // Commutative accumulation preserves duplicate casters while ignoring packet
    // order. Unlike XOR, two identical entries do not cancel one another.
    constexpr std::uint64_t MixShadowMember(std::uint64_t value) noexcept
    {
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
        return value ^ (value >> 31);
    }
    enum class ShadowFaceInvalidation : std::uint32_t
    {
        None = 0, Initial = 1, Projection = 2, CasterSet = 4, Content = 8
    };
    struct ShadowFaceKey
    {
        std::uint64_t projection = 0, casters = 0, content = 0;
    };
    // Evaluate without publishing: a key becomes valid only after its face has
    // been cleared and recorded. The policy is independent of atlas layout/RHI.
    class ShadowFaceCache
    {
    public:
        [[nodiscard]] std::uint32_t Invalidation(const ShadowFaceKey &next) const noexcept
        {
            if (!m_valid) return static_cast<std::uint32_t>(ShadowFaceInvalidation::Initial);
            return (m_key.projection != next.projection ? 2u : 0u) |
                   (m_key.casters != next.casters ? 4u : 0u) |
                   (m_key.content != next.content ? 8u : 0u);
        }
        void Publish(const ShadowFaceKey &key) noexcept { m_key = key; m_valid = true; }
        void Reset() noexcept { m_valid = false; }
    private:
        ShadowFaceKey m_key{};
        bool m_valid = false;
    };
}
