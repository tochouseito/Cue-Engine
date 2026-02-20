#pragma once
#include <cstdint>

namespace Cue::GraphicsCore
{
    struct ResourceHandle final
    {
        static constexpr uint32_t k_invalid = 0xFFFFFFFF;
        uint32_t m_index = k_invalid;
        uint16_t m_generation = 0;

        bool valid() const
        {
            // 1) 無効値かどうかを判定する
            // 2) 有効なら true を返す
            return m_index != k_invalid;
        }
    };
}
