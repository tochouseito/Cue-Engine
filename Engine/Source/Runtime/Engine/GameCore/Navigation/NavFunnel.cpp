// === Engine includes ===
#include "GameCore/Navigation/NavFunnel.h"

#include "GameCore/Navigation/NavMath.h"
#include "GameCore/Navigation/NavMeshQuery.h"

// === C++ includes ===
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace Cue::GameCore
{
    namespace
    {
        [[nodiscard]] float triangle_area_xz(
            const Math::float3& a_a,
            const Math::float3& a_b,
            const Math::float3& a_c) noexcept
        {
            return NavMath::cross_2d(
                NavMath::project_xz(a_b) - NavMath::project_xz(a_a),
                NavMath::project_xz(a_c) - NavMath::project_xz(a_a));
        }

        [[nodiscard]] bool nearly_equal(
            const Math::float3& a_left,
            const Math::float3& a_right) noexcept
        {
            return NavMath::distance_sq(a_left, a_right) <=
                NavMath::k_epsilon * NavMath::k_epsilon;
        }

        [[nodiscard]] float side_of_direction(
            const Math::float3& a_origin,
            const Math::float3& a_direction,
            const Math::float3& a_point) noexcept
        {
            return NavMath::cross_2d(
                NavMath::project_xz(a_direction),
                NavMath::project_xz(a_point - a_origin));
        }

        void orient_portal(
            const NavMeshAsset& a_asset,
            NavPolyId a_fromPolyId,
            NavPolyId a_toPolyId,
            NavPortal& a_portal) noexcept
        {
            const Math::float3& fromCenter =
                a_asset.polys[a_fromPolyId].center;
            const Math::float3 direction =
                a_asset.polys[a_toPolyId].center - fromCenter;

            if (NavMath::project_xz(direction).length_sq() <=
                NavMath::k_epsilon * NavMath::k_epsilon)
            {
                return;
            }

            const float leftSide =
                side_of_direction(fromCenter, direction, a_portal.left);
            const float rightSide =
                side_of_direction(fromCenter, direction, a_portal.right);

            if (leftSide < rightSide)
            {
                std::swap(a_portal.left, a_portal.right);
            }
        }

        void push_point_if_needed(
            std::vector<Math::float3>& a_points,
            const Math::float3& a_point)
        {
            if (a_points.empty() || !nearly_equal(a_points.back(), a_point))
            {
                a_points.push_back(a_point);
            }
        }
    }

    bool NavFunnel::build_portal_path(
        const NavMeshAsset& a_asset,
        std::span<const NavPolyId> a_polyPath,
        std::vector<NavPortal>& a_outPortals)
    {
        a_outPortals.clear();

        if (a_polyPath.empty())
        {
            return false;
        }

        if (a_polyPath.size() == 1u)
        {
            return a_asset.is_valid_poly(a_polyPath[0]);
        }

        a_outPortals.reserve(a_polyPath.size() - 1u);
        for (std::size_t i = 0; i + 1u < a_polyPath.size(); ++i)
        {
            const NavPolyId fromPolyId = a_polyPath[i];
            const NavPolyId toPolyId = a_polyPath[i + 1u];

            if (!a_asset.is_valid_poly(fromPolyId) ||
                !a_asset.is_valid_poly(toPolyId))
            {
                a_outPortals.clear();
                return false;
            }

            NavPortal portal{};
            if (!NavMeshQuery::get_shared_edge(
                    a_asset,
                    fromPolyId,
                    toPolyId,
                    portal))
            {
                a_outPortals.clear();
                return false;
            }

            orient_portal(a_asset, fromPolyId, toPolyId, portal);
            a_outPortals.push_back(portal);
        }

        return true;
    }

    bool NavFunnel::run(
        const Math::float3& a_start,
        const Math::float3& a_goal,
        std::span<const NavPortal> a_portals,
        NavPath& a_outPath)
    {
        a_outPath.points.clear();
        push_point_if_needed(a_outPath.points, a_start);

        if (a_portals.empty())
        {
            push_point_if_needed(a_outPath.points, a_goal);
            return !a_outPath.points.empty();
        }

        std::vector<NavPortal> corridor{};
        corridor.reserve(a_portals.size() + 2u);
        corridor.push_back(NavPortal{ a_start, a_start });
        corridor.insert(corridor.end(), a_portals.begin(), a_portals.end());
        corridor.push_back(NavPortal{ a_goal, a_goal });

        Math::float3 apex = corridor[0].left;
        Math::float3 left = corridor[0].left;
        Math::float3 right = corridor[0].right;
        std::size_t apexIndex = 0;
        std::size_t leftIndex = 0;
        std::size_t rightIndex = 0;

        for (std::size_t i = 1; i < corridor.size(); ++i)
        {
            const Math::float3 newLeft = corridor[i].left;
            const Math::float3 newRight = corridor[i].right;

            if (triangle_area_xz(apex, right, newRight) <=
                NavMath::k_epsilon)
            {
                if (nearly_equal(apex, right) ||
                    triangle_area_xz(apex, left, newRight) >
                        NavMath::k_epsilon)
                {
                    right = newRight;
                    rightIndex = i;
                }
                else
                {
                    push_point_if_needed(a_outPath.points, left);
                    apex = left;
                    apexIndex = leftIndex;
                    left = apex;
                    right = apex;
                    leftIndex = apexIndex;
                    rightIndex = apexIndex;
                    i = apexIndex;
                    continue;
                }
            }

            if (triangle_area_xz(apex, left, newLeft) >=
                -NavMath::k_epsilon)
            {
                if (nearly_equal(apex, left) ||
                    triangle_area_xz(apex, right, newLeft) <
                        -NavMath::k_epsilon)
                {
                    left = newLeft;
                    leftIndex = i;
                }
                else
                {
                    push_point_if_needed(a_outPath.points, right);
                    apex = right;
                    apexIndex = rightIndex;
                    left = apex;
                    right = apex;
                    leftIndex = apexIndex;
                    rightIndex = apexIndex;
                    i = apexIndex;
                    continue;
                }
            }
        }

        push_point_if_needed(a_outPath.points, a_goal);
        return !a_outPath.points.empty();
    }
}
