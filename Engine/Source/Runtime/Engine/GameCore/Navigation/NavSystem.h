#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavMeshAsset.h"
#include "GameCore/Navigation/NavMeshQuery.h"

namespace Cue::GameCore
{
    // ナビメッシュ検索の外部向け窓口。
    class NavSystem final
    {
    public:
        /// @brief 参照するナビメッシュを設定します。所有権は呼び出し側が持ちます。
        void set_asset(const NavMeshAsset* a_asset) noexcept;

        /// @brief 現在参照しているナビメッシュを返します。
        [[nodiscard]] const NavMeshAsset* get_asset() const noexcept;

        /// @brief ナビメッシュが設定済みか判定します。
        [[nodiscard]] bool has_asset() const noexcept;

        /// @brief 開始座標から目的座標までの waypoint 経路を検索します。
        [[nodiscard]] bool find_path(
            const Math::float3& a_start,
            const Math::float3& a_goal,
            const NavAgentSettings& a_agent,
            NavPath& a_outPath) const;

        /// @brief 指定座標に最も近いナビメッシュ上の点を検索します。
        [[nodiscard]] bool sample_position(
            const Math::float3& a_position,
            float a_maxDistance,
            NavSampleHit& a_outHit) const noexcept;

        /// @brief 指定座標がナビメッシュ上にあるか判定します。
        [[nodiscard]] bool is_on_nav_mesh(
            const Math::float3& a_position,
            float a_maxHeightDistance) const noexcept;

    private:
        const NavMeshAsset* m_asset = nullptr;
    };
}
