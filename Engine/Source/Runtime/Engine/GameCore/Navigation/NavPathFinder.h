#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavMeshAsset.h"

// === C++ includes ===
#include <vector>

namespace Cue::GameCore
{
    // ナビメッシュポリゴングラフ上の経路探索。
    class NavPathFinder final
    {
    public:
        /// @brief A* で開始ポリゴンから目的ポリゴンまでのポリゴン列を探します。
        [[nodiscard]] static bool find_poly_path(
            const NavMeshAsset& a_asset,
            NavPolyId a_startPolyId,
            NavPolyId a_goalPolyId,
            std::vector<NavPolyId>& a_outPolyPath);
    };
}
