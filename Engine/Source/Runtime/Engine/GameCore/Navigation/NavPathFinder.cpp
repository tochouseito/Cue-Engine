// === Engine includes ===
#include "GameCore/Navigation/NavPathFinder.h"

#include "GameCore/Navigation/NavMath.h"

// === C++ includes ===
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>

namespace Cue::GameCore
{
    namespace
    {
        struct OpenNode final
        {
            NavPolyId polyId = k_invalidNavPolyId;
            float totalCost = 0.0f;
        };

        struct OpenNodeGreater final
        {
            [[nodiscard]] bool operator()(
                const OpenNode& a_left,
                const OpenNode& a_right) const noexcept
            {
                return a_left.totalCost > a_right.totalCost;
            }
        };

        [[nodiscard]] bool is_walkable(const NavPoly& a_poly) noexcept
        {
            return a_poly.area != NavArea::Blocked;
        }

        [[nodiscard]] float heuristic_cost(
            const NavMeshAsset& a_asset,
            NavPolyId a_fromPolyId,
            NavPolyId a_toPolyId) noexcept
        {
            return NavMath::distance(
                a_asset.polys[a_fromPolyId].center,
                a_asset.polys[a_toPolyId].center);
        }

        [[nodiscard]] float edge_cost(
            const NavMeshAsset& a_asset,
            NavPolyId a_fromPolyId,
            NavPolyId a_toPolyId) noexcept
        {
            const NavPoly& toPoly = a_asset.polys[a_toPolyId];
            return NavMath::distance(
                a_asset.polys[a_fromPolyId].center,
                toPoly.center) * toPoly.cost;
        }

        void reconstruct_path(
            const std::vector<NavPolyId>& a_cameFrom,
            NavPolyId a_startPolyId,
            NavPolyId a_goalPolyId,
            std::vector<NavPolyId>& a_outPolyPath)
        {
            a_outPolyPath.clear();

            NavPolyId current = a_goalPolyId;
            while (current != k_invalidNavPolyId)
            {
                a_outPolyPath.push_back(current);

                if (current == a_startPolyId)
                {
                    break;
                }

                current = a_cameFrom[current];
            }

            std::reverse(a_outPolyPath.begin(), a_outPolyPath.end());
        }
    }

    bool NavPathFinder::find_poly_path(
        const NavMeshAsset& a_asset,
        NavPolyId a_startPolyId,
        NavPolyId a_goalPolyId,
        std::vector<NavPolyId>& a_outPolyPath)
    {
        a_outPolyPath.clear();

        if (!a_asset.is_valid_poly(a_startPolyId) ||
            !a_asset.is_valid_poly(a_goalPolyId) ||
            !is_walkable(a_asset.polys[a_startPolyId]) ||
            !is_walkable(a_asset.polys[a_goalPolyId]))
        {
            return false;
        }

        if (a_startPolyId == a_goalPolyId)
        {
            a_outPolyPath.push_back(a_startPolyId);
            return true;
        }

        const std::size_t polyCount = a_asset.polys.size();
        const float k_maxCost = (std::numeric_limits<float>::max)();

        std::vector<float> costFromStart(polyCount, k_maxCost);
        std::vector<NavPolyId> cameFrom(polyCount, k_invalidNavPolyId);
        std::vector<std::uint8_t> isClosed(polyCount, 0u);
        std::priority_queue<
            OpenNode,
            std::vector<OpenNode>,
            OpenNodeGreater>
            open{};

        costFromStart[a_startPolyId] = 0.0f;
        open.push(OpenNode{
            a_startPolyId,
            heuristic_cost(a_asset, a_startPolyId, a_goalPolyId)
        });

        while (!open.empty())
        {
            const OpenNode currentNode = open.top();
            open.pop();

            const NavPolyId currentPolyId = currentNode.polyId;
            if (!a_asset.is_valid_poly(currentPolyId))
            {
                continue;
            }

            if (isClosed[currentPolyId] != 0u)
            {
                continue;
            }

            if (currentPolyId == a_goalPolyId)
            {
                reconstruct_path(
                    cameFrom,
                    a_startPolyId,
                    a_goalPolyId,
                    a_outPolyPath);
                return !a_outPolyPath.empty();
            }

            isClosed[currentPolyId] = 1u;
            const NavPoly& currentPoly = a_asset.polys[currentPolyId];

            for (NavPolyId neighborPolyId : currentPoly.neighbors)
            {
                if (!a_asset.is_valid_poly(neighborPolyId) ||
                    isClosed[neighborPolyId] != 0u ||
                    !is_walkable(a_asset.polys[neighborPolyId]))
                {
                    continue;
                }

                const float newCost =
                    costFromStart[currentPolyId] +
                    edge_cost(a_asset, currentPolyId, neighborPolyId);

                if (newCost >= costFromStart[neighborPolyId])
                {
                    continue;
                }

                cameFrom[neighborPolyId] = currentPolyId;
                costFromStart[neighborPolyId] = newCost;
                open.push(OpenNode{
                    neighborPolyId,
                    newCost +
                        heuristic_cost(a_asset, neighborPolyId, a_goalPolyId)
                });
            }
        }

        return false;
    }
}
