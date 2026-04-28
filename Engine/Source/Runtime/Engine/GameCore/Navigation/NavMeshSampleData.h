#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavMeshAsset.h"

namespace Cue::GameCore
{
    // asset 読み込み前にナビメッシュ処理を検証するための最小データ。
    class NavMeshSampleData final
    {
    public:
        /// @brief 四角形床を三角形 2 枚のナビメッシュとして構築します。
        [[nodiscard]] static bool build_flat_quad(NavMeshAsset& a_outAsset);

        /// @brief L 字通路を三角形 6 枚のナビメッシュとして構築します。
        [[nodiscard]] static bool build_corner_corridor(
            NavMeshAsset& a_outAsset);
    };
}
