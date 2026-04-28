#pragma once

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <vector>

namespace Cue::GameCore
{
    // ナビメッシュのポリゴン識別子。
    using NavPolyId = std::uint32_t;

    // 無効なポリゴンを表す予約 ID。
    inline constexpr NavPolyId k_invalidNavPolyId =
        (std::numeric_limits<NavPolyId>::max)();

    // ポリゴンの移動領域種別。
    enum class NavArea : std::uint8_t
    {
        Walk,
        Slow,
        Blocked,
        Jump,
        Door,
    };

    // Agent の移動条件。
    struct NavAgentSettings final
    {
        float radius = 0.4f;
        float height = 1.8f;
        float maxSlope = 45.0f;
        float maxStepHeight = 0.3f;
        float maxSnapHeight = 1.0f;
    };

    // 隣接ポリゴン間の通過可能な共有エッジ。
    struct NavPortal final
    {
        Math::float3 left = Math::float3::zero();
        Math::float3 right = Math::float3::zero();
    };

    // Funnel 後に Agent が追従する経路点列。
    struct NavPath final
    {
        std::vector<Math::float3> points{};
    };
}
