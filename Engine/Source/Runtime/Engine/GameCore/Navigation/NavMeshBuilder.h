#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavMeshAsset.h"

// === C++ includes ===
#include <cstdint>
#include <span>

namespace Cue::GameCore
{
    // ナビメッシュ構築設定。
    struct NavMeshBuildSettings final
    {
        float weldScale = 1000.0f;
        float defaultCost = 1.0f;
        NavArea defaultArea = NavArea::Walk;
    };

    // 三角形入力から Runtime 用ナビメッシュを構築します。
    class NavMeshBuilder final
    {
    public:
        /// @brief 頂点配列と三角形 index 配列からナビメッシュを構築します。
        [[nodiscard]] static bool build_from_triangles(
            std::span<const Math::float3> a_vertices,
            std::span<const std::uint32_t> a_indices,
            NavMeshAsset& a_outAsset,
            const NavMeshBuildSettings& a_settings = {});
    };
}
