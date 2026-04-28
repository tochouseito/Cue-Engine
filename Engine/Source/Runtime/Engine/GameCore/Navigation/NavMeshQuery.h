#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavMeshAsset.h"

namespace Cue::GameCore
{
    // ナビメッシュ上へ補正した位置情報。
    struct NavSampleHit final
    {
        Math::float3 position = Math::float3::zero();
        NavPolyId polyId = k_invalidNavPolyId;
        float distance = 0.0f;
    };

    // ナビメッシュに対する空間問い合わせ。
    class NavMeshQuery final
    {
    public:
        /// @brief 指定座標を含むポリゴンを検索します。
        [[nodiscard]] static NavPolyId find_containing_poly(
            const NavMeshAsset& a_asset,
            const Math::float3& a_position,
            float a_maxHeightDistance) noexcept;

        /// @brief 指定座標に最も近いナビメッシュ上の点を検索します。
        [[nodiscard]] static bool sample_position(
            const NavMeshAsset& a_asset,
            const Math::float3& a_position,
            float a_maxDistance,
            NavSampleHit& a_outHit) noexcept;

        /// @brief 隣接ポリゴン間の共有エッジを取得します。
        [[nodiscard]] static bool get_shared_edge(
            const NavMeshAsset& a_asset,
            NavPolyId a_fromPolyId,
            NavPolyId a_toPolyId,
            NavPortal& a_outPortal) noexcept;
    };
}
