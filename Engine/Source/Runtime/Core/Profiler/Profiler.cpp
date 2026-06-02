// === Core includes ===
#include "Core_pch.h"
#include "Profiler.h"

// === C++ includes ===
#include <algorithm>
#include <cstring>
#include <mutex>

namespace Cue::Core::Profiler
{
    namespace
    {
        struct SampleCounters final
        {
            uint64_t callCount = 0;
            uint64_t totalNs = 0;
            uint64_t maxNs = 0;
            uint64_t lastNs = 0;

            void add(uint64_t a_elapsedNs) noexcept
            {
                ++callCount;
                totalNs += a_elapsedNs;
                maxNs = (std::max)(maxNs, a_elapsedNs);
                lastNs = a_elapsedNs;
            }

            void reset() noexcept
            {
                callCount = 0;
                totalNs = 0;
                maxNs = 0;
                lastNs = 0;
            }
        };

        struct RegisteredEntry final
        {
            EntryHandle handle{};
            const char* category = "";
            const char* name = "";
            SampleCounters current{};
            SampleCounters last{};
            bool isEnabled = true;
        };

        [[nodiscard]] const char* normalize_text(const char* a_text) noexcept
        {
            return a_text != nullptr ? a_text : "";
        }

        [[nodiscard]] double to_ms(uint64_t a_elapsedNs) noexcept
        {
            return static_cast<double>(a_elapsedNs) * 0.000001;
        }

        [[nodiscard]] EntrySnapshot make_snapshot(
            const RegisteredEntry& a_entry) noexcept
        {
            const double totalMs = to_ms(a_entry.last.totalNs);
            const double averageMs = a_entry.last.callCount > 0
                ? totalMs / static_cast<double>(a_entry.last.callCount)
                : 0.0;

            return EntrySnapshot{
                a_entry.handle,
                a_entry.category,
                a_entry.name,
                a_entry.last.callCount,
                totalMs,
                averageMs,
                to_ms(a_entry.last.maxNs),
                to_ms(a_entry.last.lastNs)
            };
        }

        class Registry final
        {
        public:
            [[nodiscard]] EntryHandle register_entry(
                const char* a_category,
                const char* a_name)
            {
                a_category = normalize_text(a_category);
                a_name = normalize_text(a_name);

                std::lock_guard lock(m_mutex);
                const auto it = std::find_if(
                    m_entries.begin(),
                    m_entries.end(),
                    [a_category, a_name](const RegisteredEntry& a_entry)
                    {
                        return std::strcmp(a_entry.category, a_category) == 0 &&
                            std::strcmp(a_entry.name, a_name) == 0;
                    });
                if (it != m_entries.end())
                {
                    return it->handle;
                }

                const EntryHandle handle{
                    static_cast<uint32_t>(m_entries.size())
                };
                m_entries.push_back(RegisteredEntry{
                    handle,
                    a_category,
                    a_name,
                    {},
                    {},
                    true });
                return handle;
            }

            void begin_frame()
            {
                std::lock_guard lock(m_mutex);
                for (RegisteredEntry& entry : m_entries)
                {
                    entry.last = entry.current;
                    entry.current.reset();
                }
            }

            void reset()
            {
                std::lock_guard lock(m_mutex);
                for (RegisteredEntry& entry : m_entries)
                {
                    entry.current.reset();
                    entry.last.reset();
                }
            }

            void set_entry_enabled(EntryHandle a_entry, bool a_isEnabled)
            {
                std::lock_guard lock(m_mutex);
                RegisteredEntry* entry = find_entry(a_entry);
                if (entry == nullptr)
                {
                    return;
                }

                entry->isEnabled = a_isEnabled;
            }

            [[nodiscard]] bool is_entry_enabled(EntryHandle a_entry)
            {
                std::lock_guard lock(m_mutex);
                const RegisteredEntry* entry = find_entry(a_entry);
                return entry != nullptr && entry->isEnabled;
            }

            void record_sample_ns(EntryHandle a_entry, uint64_t a_elapsedNs)
            {
                std::lock_guard lock(m_mutex);
                RegisteredEntry* entry = find_entry(a_entry);
                if (entry == nullptr || !entry->isEnabled)
                {
                    return;
                }

                entry->current.add(a_elapsedNs);
            }

            [[nodiscard]] std::vector<EntrySnapshot> collect_frame_snapshots()
            {
                std::lock_guard lock(m_mutex);
                std::vector<EntrySnapshot> snapshots{};
                snapshots.reserve(m_entries.size());
                for (const RegisteredEntry& entry : m_entries)
                {
                    snapshots.push_back(make_snapshot(entry));
                }
                return snapshots;
            }

        private:
            [[nodiscard]] RegisteredEntry* find_entry(EntryHandle a_entry)
            {
                if (!a_entry.is_valid() || a_entry.value >= m_entries.size())
                {
                    return nullptr;
                }

                return &m_entries[a_entry.value];
            }

            [[nodiscard]] const RegisteredEntry* find_entry(
                EntryHandle a_entry) const
            {
                if (!a_entry.is_valid() || a_entry.value >= m_entries.size())
                {
                    return nullptr;
                }

                return &m_entries[a_entry.value];
            }

            std::mutex m_mutex{};
            std::vector<RegisteredEntry> m_entries{};
        };

        [[nodiscard]] Registry& registry()
        {
            static Registry instance{};
            return instance;
        }
    }

    EntryHandle register_entry(const char* a_category, const char* a_name)
    {
        return registry().register_entry(a_category, a_name);
    }

    void begin_frame()
    {
        registry().begin_frame();
    }

    void reset()
    {
        registry().reset();
    }

    void set_entry_enabled(EntryHandle a_entry, bool a_isEnabled)
    {
        registry().set_entry_enabled(a_entry, a_isEnabled);
    }

    bool is_entry_enabled(EntryHandle a_entry)
    {
        return registry().is_entry_enabled(a_entry);
    }

    void record_sample_ns(EntryHandle a_entry, uint64_t a_elapsedNs)
    {
        registry().record_sample_ns(a_entry, a_elapsedNs);
    }

    std::vector<EntrySnapshot> collect_frame_snapshots()
    {
        return registry().collect_frame_snapshots();
    }

    ScopedSample::ScopedSample(EntryHandle a_entry)
        : m_entry(a_entry)
        , m_startedAt(Clock::now())
        , m_isActive(is_entry_enabled(a_entry))
    {
    }

    ScopedSample::~ScopedSample()
    {
        if (!m_isActive)
        {
            return;
        }

        const Clock::time_point finishedAt = Clock::now();
        const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            finishedAt - m_startedAt);
        record_sample_ns(m_entry, static_cast<uint64_t>(elapsedNs.count()));
    }
}
