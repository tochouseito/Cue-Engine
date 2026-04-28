#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavTypes.h"

// === C++ includes ===
#include <array>
#include <cstdint>
#include <vector>

namespace Cue::GameCore
{
    // ナビメッシュ頂点。
    struct NavVertex final
    {
        Math::float3 position = Math::float3::zero();
    };

    // 三角形ナビメッシュポリゴン。
    struct NavPoly final
    {
        std::array<std::uint32_t, 3> indices{ 0, 0, 0 };
        std::array<NavPolyId, 3> neighbors{
            k_invalidNavPolyId,
            k_invalidNavPolyId,
            k_invalidNavPolyId
        };
        Math::float3 center = Math::float3::zero();
        float cost = 1.0f;
        NavArea area = NavArea::Walk;
    };

    // Runtime が参照する完成済みナビメッシュデータ。
    struct NavMeshAsset final
    {
        std::vector<NavVertex> vertices{};
        std::vector<NavPoly> polys{};

        /// @brief すべてのナビメッシュデータを破棄します。
        void clear() noexcept;

        /// @brief ポリゴン ID が有効範囲内か判定します。
        [[nodiscard]] bool is_valid_poly(NavPolyId a_polyId) const noexcept;

        /// @brief 各ポリゴンの中心点を頂点配列から再計算します。
        void compute_poly_centers() noexcept;
    };
}
