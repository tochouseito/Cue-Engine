// === Engine includes ===
#include "GameCore/Navigation/NavDebugDraw.h"

// === C++ includes ===
#include <cstddef>
#include <cstdint>

namespace Cue::GameCore
{
    namespace
    {
        [[nodiscard]] bool get_edge(
            const NavMeshAsset& a_asset,
            const NavPoly& a_poly,
            std::uint32_t a_edgeIndex,
            Math::float3& a_outFrom,
            Math::float3& a_outTo) noexcept
        {
            const std::uint32_t fromIndex = a_poly.indices[a_edgeIndex];
            const std::uint32_t toIndex =
                a_poly.indices[(a_edgeIndex + 1u) % 3u];

            if (fromIndex >= a_asset.vertices.size() ||
                toIndex >= a_asset.vertices.size())
            {
                return false;
            }

            a_outFrom = a_asset.vertices[fromIndex].position;
            a_outTo = a_asset.vertices[toIndex].position;
            return true;
        }

        void append_line(
            const Math::float3& a_from,
            const Math::float3& a_to,
            NavDebugLineKind a_kind,
            std::vector<NavDebugLine>& a_outLines)
        {
            a_outLines.push_back(NavDebugLine{ a_from, a_to, a_kind });
        }
    }

    void NavDebugDraw::append_nav_mesh(
        const NavMeshAsset& a_asset,
        std::vector<NavDebugLine>& a_outLines)
    {
        a_outLines.reserve(a_outLines.size() + a_asset.polys.size() * 3u);

        for (const NavPoly& poly : a_asset.polys)
        {
            for (std::uint32_t edgeIndex = 0; edgeIndex < 3u; ++edgeIndex)
            {
                Math::float3 from{};
                Math::float3 to{};
                if (!get_edge(a_asset, poly, edgeIndex, from, to))
                {
                    continue;
                }

                const NavDebugLineKind kind =
                    poly.neighbors[edgeIndex] == k_invalidNavPolyId
                        ? NavDebugLineKind::BoundaryEdge
                        : NavDebugLineKind::ConnectedEdge;
                append_line(from, to, kind, a_outLines);
            }
        }
    }

    void NavDebugDraw::append_poly_path(
        const NavMeshAsset& a_asset,
        std::span<const NavPolyId> a_polyPath,
        std::vector<NavDebugLine>& a_outLines)
    {
        if (a_polyPath.size() < 2u)
        {
            return;
        }

        a_outLines.reserve(a_outLines.size() + a_polyPath.size() - 1u);
        for (std::size_t i = 0; i + 1u < a_polyPath.size(); ++i)
        {
            const NavPolyId fromPolyId = a_polyPath[i];
            const NavPolyId toPolyId = a_polyPath[i + 1u];
            if (!a_asset.is_valid_poly(fromPolyId) ||
                !a_asset.is_valid_poly(toPolyId))
            {
                continue;
            }

            append_line(
                a_asset.polys[fromPolyId].center,
                a_asset.polys[toPolyId].center,
                NavDebugLineKind::PolyPath,
                a_outLines);
        }
    }

    void NavDebugDraw::append_portals(
        std::span<const NavPortal> a_portals,
        std::vector<NavDebugLine>& a_outLines)
    {
        a_outLines.reserve(a_outLines.size() + a_portals.size());
        for (const NavPortal& portal : a_portals)
        {
            append_line(
                portal.left,
                portal.right,
                NavDebugLineKind::Portal,
                a_outLines);
        }
    }

    void NavDebugDraw::append_path(
        const NavPath& a_path,
        std::vector<NavDebugLine>& a_outLines)
    {
        if (a_path.points.size() < 2u)
        {
            return;
        }

        a_outLines.reserve(a_outLines.size() + a_path.points.size() - 1u);
        for (std::size_t i = 0; i + 1u < a_path.points.size(); ++i)
        {
            append_line(
                a_path.points[i],
                a_path.points[i + 1u],
                NavDebugLineKind::PathSegment,
                a_outLines);
        }
    }

    void NavDebugDraw::append_agent_target(
        const Math::float3& a_position,
        const Math::float3& a_target,
        std::vector<NavDebugLine>& a_outLines)
    {
        append_line(
            a_position,
            a_target,
            NavDebugLineKind::AgentTarget,
            a_outLines);
    }
}
