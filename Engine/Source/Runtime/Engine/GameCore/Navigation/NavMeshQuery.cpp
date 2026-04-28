// === Engine includes ===
#include "GameCore/Navigation/NavMeshQuery.h"

#include "GameCore/Navigation/NavMath.h"

// === C++ includes ===
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Cue::GameCore
{
    namespace
    {
        [[nodiscard]] bool is_walkable(const NavPoly& a_poly) noexcept
        {
            return a_poly.area != NavArea::Blocked;
        }

        [[nodiscard]] bool can_store_poly_id(std::size_t a_index) noexcept
        {
            return a_index <=
                static_cast<std::size_t>(
                    (std::numeric_limits<NavPolyId>::max)());
        }

        [[nodiscard]] bool get_triangle_vertices(
            const NavMeshAsset& a_asset,
            const NavPoly& a_poly,
            Math::float3& a_outA,
            Math::float3& a_outB,
            Math::float3& a_outC) noexcept
        {
            const bool hasValidIndices =
                a_poly.indices[0] < a_asset.vertices.size() &&
                a_poly.indices[1] < a_asset.vertices.size() &&
                a_poly.indices[2] < a_asset.vertices.size();

            if (!hasValidIndices)
            {
                return false;
            }

            a_outA = a_asset.vertices[a_poly.indices[0]].position;
            a_outB = a_asset.vertices[a_poly.indices[1]].position;
            a_outC = a_asset.vertices[a_poly.indices[2]].position;
            return true;
        }
    }

    NavPolyId NavMeshQuery::find_containing_poly(
        const NavMeshAsset& a_asset,
        const Math::float3& a_position,
        float a_maxHeightDistance) noexcept
    {
        if (a_maxHeightDistance < 0.0f)
        {
            return k_invalidNavPolyId;
        }

        NavPolyId bestPolyId = k_invalidNavPolyId;
        float bestHeightDiff = (std::numeric_limits<float>::max)();

        for (std::size_t polyIndex = 0; polyIndex < a_asset.polys.size();
             ++polyIndex)
        {
            if (!can_store_poly_id(polyIndex))
            {
                break;
            }

            const NavPoly& poly = a_asset.polys[polyIndex];
            if (!is_walkable(poly))
            {
                continue;
            }

            Math::float3 a{};
            Math::float3 b{};
            Math::float3 c{};
            if (!get_triangle_vertices(a_asset, poly, a, b, c))
            {
                continue;
            }

            if (!NavMath::point_in_triangle_xz(a_position, a, b, c))
            {
                continue;
            }

            float height = 0.0f;
            if (!NavMath::triangle_height_at_xz(a_position, a, b, c, height))
            {
                continue;
            }

            const float heightDiff = std::fabs(a_position.y - height);
            if (heightDiff <= a_maxHeightDistance &&
                heightDiff < bestHeightDiff)
            {
                bestHeightDiff = heightDiff;
                bestPolyId = static_cast<NavPolyId>(polyIndex);
            }
        }

        return bestPolyId;
    }

    bool NavMeshQuery::sample_position(
        const NavMeshAsset& a_asset,
        const Math::float3& a_position,
        float a_maxDistance,
        NavSampleHit& a_outHit) noexcept
    {
        if (a_maxDistance < 0.0f)
        {
            return false;
        }

        const float maxDistanceSq = a_maxDistance * a_maxDistance;
        float bestDistanceSq = (std::numeric_limits<float>::max)();
        NavSampleHit bestHit{};

        for (std::size_t polyIndex = 0; polyIndex < a_asset.polys.size();
             ++polyIndex)
        {
            if (!can_store_poly_id(polyIndex))
            {
                break;
            }

            const NavPoly& poly = a_asset.polys[polyIndex];
            if (!is_walkable(poly))
            {
                continue;
            }

            Math::float3 a{};
            Math::float3 b{};
            Math::float3 c{};
            if (!get_triangle_vertices(a_asset, poly, a, b, c))
            {
                continue;
            }

            const Math::float3 closest =
                NavMath::closest_point_on_triangle(a_position, a, b, c);
            const float distanceSq = NavMath::distance_sq(a_position, closest);

            if (distanceSq <= maxDistanceSq && distanceSq < bestDistanceSq)
            {
                bestDistanceSq = distanceSq;
                bestHit.position = closest;
                bestHit.polyId = static_cast<NavPolyId>(polyIndex);
            }
        }

        if (bestHit.polyId == k_invalidNavPolyId)
        {
            return false;
        }

        bestHit.distance = std::sqrt(bestDistanceSq);
        a_outHit = bestHit;
        return true;
    }

    bool NavMeshQuery::get_shared_edge(
        const NavMeshAsset& a_asset,
        NavPolyId a_fromPolyId,
        NavPolyId a_toPolyId,
        NavPortal& a_outPortal) noexcept
    {
        if (!a_asset.is_valid_poly(a_fromPolyId) ||
            !a_asset.is_valid_poly(a_toPolyId))
        {
            return false;
        }

        const NavPoly& fromPoly = a_asset.polys[a_fromPolyId];
        for (std::uint32_t edgeIndex = 0; edgeIndex < 3u; ++edgeIndex)
        {
            if (fromPoly.neighbors[edgeIndex] != a_toPolyId)
            {
                continue;
            }

            const std::uint32_t leftIndex = fromPoly.indices[edgeIndex];
            const std::uint32_t rightIndex =
                fromPoly.indices[(edgeIndex + 1u) % 3u];

            if (leftIndex >= a_asset.vertices.size() ||
                rightIndex >= a_asset.vertices.size())
            {
                return false;
            }

            a_outPortal.left = a_asset.vertices[leftIndex].position;
            a_outPortal.right = a_asset.vertices[rightIndex].position;
            return true;
        }

        return false;
    }
}
