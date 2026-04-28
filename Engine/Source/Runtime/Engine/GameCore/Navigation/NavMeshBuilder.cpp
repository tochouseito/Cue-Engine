// === Engine includes ===
#include "GameCore/Navigation/NavMeshBuilder.h"

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace Cue::GameCore
{
    namespace
    {
        struct QuantizedPoint final
        {
            std::int32_t x = 0;
            std::int32_t y = 0;
            std::int32_t z = 0;

            [[nodiscard]] bool operator==(
                const QuantizedPoint& a_other) const noexcept
            {
                return x == a_other.x &&
                    y == a_other.y &&
                    z == a_other.z;
            }

            [[nodiscard]] bool operator<(
                const QuantizedPoint& a_other) const noexcept
            {
                if (x != a_other.x)
                {
                    return x < a_other.x;
                }

                if (y != a_other.y)
                {
                    return y < a_other.y;
                }

                return z < a_other.z;
            }
        };

        struct EdgeKey final
        {
            QuantizedPoint a{};
            QuantizedPoint b{};

            [[nodiscard]] bool operator==(const EdgeKey& a_other) const noexcept
            {
                return a == a_other.a && b == a_other.b;
            }
        };

        struct EdgeRef final
        {
            NavPolyId polyId = k_invalidNavPolyId;
            std::uint32_t edgeIndex = 0;
        };

        struct EdgeKeyHash final
        {
            [[nodiscard]] std::size_t operator()(
                const EdgeKey& a_key) const noexcept
            {
                std::size_t seed = 0;
                combine(seed, a_key.a.x);
                combine(seed, a_key.a.y);
                combine(seed, a_key.a.z);
                combine(seed, a_key.b.x);
                combine(seed, a_key.b.y);
                combine(seed, a_key.b.z);
                return seed;
            }

            static void combine(
                std::size_t& a_seed,
                std::int32_t a_value) noexcept
            {
                const std::size_t hash = std::hash<std::int32_t>{}(a_value);
                a_seed ^= hash + 0x9e3779b9u + (a_seed << 6) + (a_seed >> 2);
            }
        };

        [[nodiscard]] bool is_valid_settings(
            const NavMeshBuildSettings& a_settings) noexcept
        {
            return a_settings.weldScale > 0.0f &&
                a_settings.defaultCost > 0.0f;
        }

        [[nodiscard]] bool can_store_poly_count(std::size_t a_count) noexcept
        {
            return a_count <=
                static_cast<std::size_t>(
                    (std::numeric_limits<NavPolyId>::max)());
        }

        [[nodiscard]] QuantizedPoint quantize_point(
            const Math::float3& a_position,
            float a_scale) noexcept
        {
            return QuantizedPoint{
                static_cast<std::int32_t>(std::lround(a_position.x * a_scale)),
                static_cast<std::int32_t>(std::lround(a_position.y * a_scale)),
                static_cast<std::int32_t>(std::lround(a_position.z * a_scale))
            };
        }

        [[nodiscard]] EdgeKey make_edge_key(
            const Math::float3& a_left,
            const Math::float3& a_right,
            float a_scale) noexcept
        {
            std::array<QuantizedPoint, 2> points{
                quantize_point(a_left, a_scale),
                quantize_point(a_right, a_scale)
            };
            std::sort(points.begin(), points.end());
            return EdgeKey{ points[0], points[1] };
        }

        [[nodiscard]] bool validate_indices(
            std::span<const std::uint32_t> a_indices,
            std::size_t a_vertexCount) noexcept
        {
            for (const std::uint32_t index : a_indices)
            {
                if (static_cast<std::size_t>(index) >= a_vertexCount)
                {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] bool build_neighbors(
            NavMeshAsset& a_asset,
            float a_weldScale)
        {
            std::unordered_map<EdgeKey, EdgeRef, EdgeKeyHash> edgeMap{};
            edgeMap.reserve(a_asset.polys.size() * 3u);

            for (std::size_t polyIndex = 0; polyIndex < a_asset.polys.size();
                 ++polyIndex)
            {
                NavPoly& poly = a_asset.polys[polyIndex];
                const NavPolyId polyId = static_cast<NavPolyId>(polyIndex);

                for (std::uint32_t edgeIndex = 0; edgeIndex < 3u; ++edgeIndex)
                {
                    const std::uint32_t leftIndex = poly.indices[edgeIndex];
                    const std::uint32_t rightIndex =
                        poly.indices[(edgeIndex + 1u) % 3u];

                    const EdgeKey key = make_edge_key(
                        a_asset.vertices[leftIndex].position,
                        a_asset.vertices[rightIndex].position,
                        a_weldScale);
                    auto found = edgeMap.find(key);

                    if (found == edgeMap.end())
                    {
                        edgeMap.emplace(key, EdgeRef{ polyId, edgeIndex });
                        continue;
                    }

                    const EdgeRef other = found->second;
                    if (other.polyId == k_invalidNavPolyId ||
                        !a_asset.is_valid_poly(other.polyId))
                    {
                        return false;
                    }

                    NavPoly& otherPoly = a_asset.polys[other.polyId];
                    if (poly.neighbors[edgeIndex] != k_invalidNavPolyId ||
                        otherPoly.neighbors[other.edgeIndex] !=
                            k_invalidNavPolyId)
                    {
                        return false;
                    }

                    poly.neighbors[edgeIndex] = other.polyId;
                    otherPoly.neighbors[other.edgeIndex] = polyId;
                }
            }

            return true;
        }
    }

    bool NavMeshBuilder::build_from_triangles(
        std::span<const Math::float3> a_vertices,
        std::span<const std::uint32_t> a_indices,
        NavMeshAsset& a_outAsset,
        const NavMeshBuildSettings& a_settings)
    {
        constexpr std::size_t k_triangleIndexCount = 3u;

        const bool hasValidInput =
            !a_vertices.empty() &&
            !a_indices.empty() &&
            a_indices.size() % k_triangleIndexCount == 0u &&
            is_valid_settings(a_settings) &&
            can_store_poly_count(a_indices.size() / k_triangleIndexCount) &&
            validate_indices(a_indices, a_vertices.size());

        if (!hasValidInput)
        {
            return false;
        }

        NavMeshAsset asset{};
        asset.vertices.reserve(a_vertices.size());
        for (const Math::float3& position : a_vertices)
        {
            asset.vertices.push_back(NavVertex{ position });
        }

        const std::size_t triangleCount =
            a_indices.size() / k_triangleIndexCount;
        asset.polys.reserve(triangleCount);

        for (std::size_t triangleIndex = 0; triangleIndex < triangleCount;
             ++triangleIndex)
        {
            const std::size_t baseIndex = triangleIndex * k_triangleIndexCount;
            NavPoly poly{};
            poly.indices = {
                a_indices[baseIndex],
                a_indices[baseIndex + 1u],
                a_indices[baseIndex + 2u]
            };
            poly.cost = a_settings.defaultCost;
            poly.area = a_settings.defaultArea;
            asset.polys.push_back(poly);
        }

        asset.compute_poly_centers();

        if (!build_neighbors(asset, a_settings.weldScale))
        {
            return false;
        }

        a_outAsset = std::move(asset);
        return true;
    }
}
