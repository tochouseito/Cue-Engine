// ShadowAtlasAllocator の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include <ShadowSystem/GpuData/ShadowData.h>

// === C++ includes ===
#include <array>
#include <cstdint>

namespace Cue::ShadowSystem
{
    class ShadowAtlasAllocator final
    {
    public:
        void reset() noexcept
        {
            m_nextSlot = 0;
        }

        [[nodiscard]] bool allocate(
            uint32_t& a_outSlot,
            Math::float4& a_outAtlas) noexcept
        {
            if (m_nextSlot >= GpuData::k_maxSpotShadowCount)
            {
                return false;
            }

            a_outSlot = m_nextSlot;
            ++m_nextSlot;

            const uint32_t tileX =
                a_outSlot % GpuData::k_spotShadowAtlasColumnCount;
            const uint32_t tileY =
                a_outSlot / GpuData::k_spotShadowAtlasColumnCount;
            a_outAtlas = Math::float4(
                static_cast<float>(tileX) /
                    static_cast<float>(GpuData::k_spotShadowAtlasColumnCount),
                static_cast<float>(tileY) /
                    static_cast<float>(GpuData::k_spotShadowAtlasRowCount),
                1.0f /
                    static_cast<float>(GpuData::k_spotShadowAtlasColumnCount),
                1.0f /
                    static_cast<float>(GpuData::k_spotShadowAtlasRowCount));
            return true;
        }

    private:
        uint32_t m_nextSlot = 0;
    };
}
