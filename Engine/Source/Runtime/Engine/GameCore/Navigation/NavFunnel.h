#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavMeshAsset.h"

// === C++ includes ===
#include <span>
#include <vector>

namespace Cue::GameCore
{
    // ポリゴン列から自然な waypoint 列を生成します。
    class NavFunnel final
    {
    public:
        /// @brief ポリゴン列から共有エッジのポータル列を作ります。
        [[nodiscard]] static bool build_portal_path(
            const NavMeshAsset& a_asset,
            std::span<const NavPolyId> a_polyPath,
            std::vector<NavPortal>& a_outPortals);

        /// @brief ポータル列を Funnel Algorithm で waypoint に変換します。
        [[nodiscard]] static bool run(
            const Math::float3& a_start,
            const Math::float3& a_goal,
            std::span<const NavPortal> a_portals,
            NavPath& a_outPath);
    };
}
