// ParticleRangeAllocator の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include <GpuData/Particle.h>

// === C++ includes ===
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Cue::ParticleSystem
{
    class ParticleRangeAllocator final
    {
    public:
        [[nodiscard]] bool allocate(
            uint32_t a_requestedCapacity,
            uint32_t& a_outBase,
            uint32_t& a_outCapacity) noexcept
        {
            const uint32_t capacity = (std::clamp)(
                a_requestedCapacity,
                1u,
                GpuData::k_maxParticleCount);
            for (auto it = m_freeRanges.begin(); it != m_freeRanges.end(); ++it)
            {
                if (it->capacity < capacity)
                {
                    continue;
                }

                a_outBase = it->base;
                a_outCapacity = capacity;
                it->base += capacity;
                it->capacity -= capacity;
                if (it->capacity == 0)
                {
                    m_freeRanges.erase(it);
                }
                return true;
            }

            if (m_nextParticleBase + capacity > GpuData::k_maxParticleCount)
            {
                a_outBase = GpuData::k_maxParticleCount;
                a_outCapacity = 0;
                return false;
            }

            a_outBase = m_nextParticleBase;
            a_outCapacity = capacity;
            m_nextParticleBase += capacity;
            return true;
        }

        void release(uint32_t a_base, uint32_t a_capacity)
        {
            if (a_capacity == 0 || a_base >= GpuData::k_maxParticleCount)
            {
                return;
            }

            const uint32_t end = (std::min)(
                a_base + a_capacity,
                GpuData::k_maxParticleCount);
            m_freeRanges.push_back(FreeRange{ a_base, end - a_base });
            merge_free_ranges();
        }

    private:
        struct FreeRange final
        {
            uint32_t base = 0;
            uint32_t capacity = 0;
        };

        void merge_free_ranges()
        {
            std::sort(
                m_freeRanges.begin(),
                m_freeRanges.end(),
                [](const FreeRange& a_left, const FreeRange& a_right)
                {
                    return a_left.base < a_right.base;
                });

            size_t writeIndex = 0;
            for (const FreeRange& range : m_freeRanges)
            {
                if (range.capacity == 0)
                {
                    continue;
                }

                if (writeIndex == 0)
                {
                    m_freeRanges[writeIndex++] = range;
                    continue;
                }

                FreeRange& previous = m_freeRanges[writeIndex - 1];
                const uint32_t previousEnd = previous.base + previous.capacity;
                if (range.base <= previousEnd)
                {
                    const uint32_t rangeEnd = range.base + range.capacity;
                    previous.capacity = (std::max)(previousEnd, rangeEnd) -
                        previous.base;
                    continue;
                }

                m_freeRanges[writeIndex++] = range;
            }

            m_freeRanges.resize(writeIndex);
        }

        std::vector<FreeRange> m_freeRanges{};
        uint32_t m_nextParticleBase = 0;
    };
} // namespace Cue::ParticleSystem
