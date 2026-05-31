#pragma once

/// *********************************************************************************
/// 数学機能集約ヘッダ
/// *********************************************************************************

// 行ベクトル row-vector 前提
// 行列は行優先 row-major 前提

#define NOMINMAX // Windows.h の min/max マクロを無効化

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Math includes ===
#include "TimeUnit.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4.h"
#include "Quaternion.h"

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <numbers>

namespace Cue::Math
{
    /// @brief 値を指定された倍数に切り上げ
    /// @param a_value 切り上げる値
    /// @param a_step 切り上げ先の倍数0の場合は無効
    /// @return 指定された倍数に切り上げられた値
    [[nodiscard]] uint32_t round_up_to_multiple(uint32_t a_value, uint32_t a_step) noexcept;
}
