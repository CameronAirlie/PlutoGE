#pragma once
#include <atomic>
#include <cstdint>

namespace PlutoGE::render
{
    // Process-local identity, never an address or a serialized asset ID. Copies
    // represent new producers, preventing stale cache hits after cloning/reuse.
    class RenderObjectIdentity
    {
    public:
        RenderObjectIdentity() : m_value(++s_next) {}
        RenderObjectIdentity(const RenderObjectIdentity &) : RenderObjectIdentity() {}
        RenderObjectIdentity &operator=(const RenderObjectIdentity &other)
        { if (this != &other) m_value = ++s_next; return *this; }
        [[nodiscard]] std::uint64_t Value() const noexcept { return m_value; }
    private:
        inline static std::atomic<std::uint64_t> s_next{0};
        std::uint64_t m_value;
    };
}
