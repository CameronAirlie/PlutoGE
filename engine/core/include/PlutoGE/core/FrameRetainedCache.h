#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PlutoGE::core
{
    // How long a FrameRetainedCache keeps unused entries, and how quickly its sweep revisits them.
    struct FrameRetentionPolicy
    {
        // Entries unused for more than this many frames expire.
        std::uint64_t retainFrames = 240;
        // Sweep visits every entry once across this many calls, so an expired entry is evicted at most this many
        // frames after it expires.
        std::uint64_t sweepFrames = 64;
    };

    // A keyed cache whose entries expire after going unused for a number of frames.
    //
    // Erasing every stale entry at a fixed interval costs nothing on most frames and a great deal on the sweep frame
    // when keys churn (a packet cache keyed by revisions, for instance, abandons an entry every time its source
    // changes). Sweep instead visits a fixed share of the entries every frame, so eviction cost is spread evenly and
    // stays proportional to the cache's size divided by the sweep period.
    //
    // Values live at stable addresses: references and pointers returned by Use and Find stay valid until that entry
    // is evicted or the cache is cleared, however many entries are added meanwhile.
    template <class Key, class Value, class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>>
    class FrameRetainedCache
    {
    public:
        struct Lookup
        {
            Value &value;
            // True when the entry did not exist and was value-initialised by this call.
            bool created;
        };

        explicit FrameRetainedCache(FrameRetentionPolicy policy = {})
            : m_policy{policy.retainFrames, std::max<std::uint64_t>(policy.sweepFrames, 1)}
        {
        }

        // Advances the cache's frame. Entries used after this call count as used in the new frame.
        std::uint64_t BeginFrame() noexcept { return ++m_frame; }
        [[nodiscard]] std::uint64_t Frame() const noexcept { return m_frame; }
        [[nodiscard]] const FrameRetentionPolicy &Policy() const noexcept { return m_policy; }

        // Returns the entry for `key`, creating it when absent, and marks it used this frame.
        Lookup Use(const Key &key)
        {
            const auto [found, inserted] = m_index.try_emplace(key, 0u);
            if (!inserted)
            {
                auto &slot = m_slots[found->second];
                slot.lastUsed = m_frame;
                return {slot.value, false};
            }
            std::size_t index;
            if (!m_free.empty())
            {
                index = m_free.back();
                m_free.pop_back();
            }
            else
            {
                index = m_slots.size();
                m_slots.emplace_back();
            }
            found->second = index;
            auto &slot = m_slots[index];
            slot.key = key;
            slot.lastUsed = m_frame;
            slot.live = true;
            return {slot.value, true};
        }

        // The entry for `key`, or null. Does not mark it used.
        [[nodiscard]] Value *Find(const Key &key)
        {
            const auto found = m_index.find(key);
            return found == m_index.end() ? nullptr : &m_slots[found->second].value;
        }
        [[nodiscard]] const Value *Find(const Key &key) const
        {
            const auto found = m_index.find(key);
            return found == m_index.end() ? nullptr : &m_slots[found->second].value;
        }

        // Visits the next share of entries, evicting those `expired(key, value, framesUnused)` selects. `onEvict`
        // receives each value before its entry is removed (to release resources the value refers to). Entries used
        // this frame are never evicted, so references obtained this frame stay valid. Returns the number evicted.
        template <class Expired, class OnEvict>
        std::size_t Sweep(Expired &&expired, OnEvict &&onEvict)
        {
            if (m_slots.empty())
                return 0;
            const auto share = (m_slots.size() + m_policy.sweepFrames - 1) / m_policy.sweepFrames;
            std::size_t evicted = 0;
            for (std::size_t visited = 0; visited < share; ++visited)
            {
                if (m_cursor >= m_slots.size())
                    m_cursor = 0;
                auto &slot = m_slots[m_cursor++];
                if (!slot.live || slot.lastUsed == m_frame)
                    continue;
                if (!expired(std::as_const(slot.key), std::as_const(slot.value), m_frame - slot.lastUsed))
                    continue;
                onEvict(slot.value);
                Release(m_cursor - 1);
                ++evicted;
            }
            return evicted;
        }
        // Sweeps with the policy's retention: entries unused for more than retainFrames frames expire.
        template <class OnEvict>
        std::size_t Sweep(OnEvict &&onEvict)
        {
            const auto retain = m_policy.retainFrames;
            return Sweep([retain](const Key &, const Value &, std::uint64_t unused) { return unused > retain; },
                         std::forward<OnEvict>(onEvict));
        }
        std::size_t Sweep()
        {
            return Sweep([](Value &) {});
        }

        // Calls fn(key, value) for every entry.
        template <class Fn>
        void ForEach(Fn &&fn)
        {
            for (auto &slot : m_slots)
                if (slot.live)
                    fn(std::as_const(slot.key), slot.value);
        }

        // Removes every entry, passing each value to `onEvict` first.
        template <class OnEvict>
        void Clear(OnEvict &&onEvict)
        {
            for (auto &slot : m_slots)
                if (slot.live)
                    onEvict(slot.value);
            m_slots.clear();
            m_free.clear();
            m_index.clear();
            m_cursor = 0;
        }
        void Clear()
        {
            Clear([](Value &) {});
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_index.size(); }
        [[nodiscard]] bool Empty() const noexcept { return m_index.empty(); }

    private:
        struct Slot
        {
            Key key{};
            Value value{};
            std::uint64_t lastUsed = 0;
            bool live = false;
        };

        void Release(std::size_t index)
        {
            auto &slot = m_slots[index];
            m_index.erase(slot.key);
            // Drop what the value holds now rather than when the slot is reused.
            slot.value = Value{};
            slot.key = Key{};
            slot.live = false;
            m_free.push_back(index);
        }

        FrameRetentionPolicy m_policy;
        // A deque keeps values at stable addresses as slots are added.
        std::deque<Slot> m_slots;
        std::vector<std::size_t> m_free;
        std::unordered_map<Key, std::size_t, Hash, KeyEqual> m_index;
        std::uint64_t m_frame = 0;
        std::size_t m_cursor = 0;
    };
}
