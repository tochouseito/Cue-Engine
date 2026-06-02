// MathStructAllowedList の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstdint>
#include <concepts>
#include <type_traits>

namespace Cue::Math
{
    template <class T>
    concept AllowedVector =
        std::is_same_v<T, int> ||
        std::is_same_v<T, unsigned int> ||
        std::is_same_v<T, std::uint32_t> ||
        std::is_same_v<T, float> ||
        std::is_same_v<T, double>;
}
