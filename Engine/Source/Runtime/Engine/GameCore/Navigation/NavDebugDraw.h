#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavMeshAsset.h"

// === C++ includes ===
#include <cstdint>
#include <span>
#include <vector>

namespace Cue::GameCore
{
    // デバッグ線分の用途。
    enum class NavDebugLineKind : std::uint8_t
    {
        PolyEdge,
        ConnectedEdge,
        BoundaryEdge,
        PolyPath,
        Portal,
        PathSegment,
        AgentTarget,
    };

    // renderer へ渡すナビメッシュ用デバッグ線分。
    struct NavDebugLine final
    {
        Math::float3 from = Math::float3::zero();
        Math::float3 to = Math::float3::zero();
        NavDebugLineKind kind = NavDebugLineKind::PolyEdge;
    };

    // ナビメッシュ関連の可視化用線分を生成します。
    class NavDebugDraw final
    {
    public:
        /// @brief ナビメッシュの三角形辺を線分として追加します。
        static void append_nav_mesh(
            const NavMeshAsset& a_asset,
            std::vector<NavDebugLine>& a_outLines);

        /// @brief A* のポリゴン列を中心点の線分として追加します。
        static void append_poly_path(
            const NavMeshAsset& a_asset,
            std::span<const NavPolyId> a_polyPath,
            std::vector<NavDebugLine>& a_outLines);

        /// @brief ポータル列を線分として追加します。
        static void append_portals(
            std::span<const NavPortal> a_portals,
            std::vector<NavDebugLine>& a_outLines);

        /// @brief Funnel 後の waypoint 経路を線分として追加します。
        static void append_path(
            const NavPath& a_path,
            std::vector<NavDebugLine>& a_outLines);

        /// @brief Agent の現在位置から追従対象までの線分を追加します。
        static void append_agent_target(
            const Math::float3& a_position,
            const Math::float3& a_target,
            std::vector<NavDebugLine>& a_outLines);
    };
}
