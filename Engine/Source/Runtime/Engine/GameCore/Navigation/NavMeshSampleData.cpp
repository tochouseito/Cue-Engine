// === Engine includes ===
#include "GameCore/Navigation/NavMeshSampleData.h"

#include "GameCore/Navigation/NavMeshBuilder.h"

// === C++ includes ===
#include <array>
#include <cstdint>

namespace Cue::GameCore
{
    bool NavMeshSampleData::build_flat_quad(NavMeshAsset& a_outAsset)
    {
        const std::array<Math::float3, 4> vertices{
            Math::float3(-1.0f, 0.0f, -1.0f),
            Math::float3(1.0f, 0.0f, -1.0f),
            Math::float3(1.0f, 0.0f, 1.0f),
            Math::float3(-1.0f, 0.0f, 1.0f)
        };

        const std::array<std::uint32_t, 6> indices{
            0u,
            1u,
            2u,
            0u,
            2u,
            3u
        };

        return NavMeshBuilder::build_from_triangles(
            vertices,
            indices,
            a_outAsset);
    }

    bool NavMeshSampleData::build_corner_corridor(NavMeshAsset& a_outAsset)
    {
        const std::array<Math::float3, 12> vertices{
            Math::float3(0.0f, 0.0f, 0.0f),
            Math::float3(2.0f, 0.0f, 0.0f),
            Math::float3(2.0f, 0.0f, 2.0f),
            Math::float3(0.0f, 0.0f, 2.0f),

            Math::float3(2.0f, 0.0f, 0.0f),
            Math::float3(4.0f, 0.0f, 0.0f),
            Math::float3(4.0f, 0.0f, 2.0f),
            Math::float3(2.0f, 0.0f, 2.0f),

            Math::float3(2.0f, 0.0f, 2.0f),
            Math::float3(4.0f, 0.0f, 2.0f),
            Math::float3(4.0f, 0.0f, 4.0f),
            Math::float3(2.0f, 0.0f, 4.0f)
        };

        const std::array<std::uint32_t, 18> indices{
            0u,
            1u,
            2u,
            0u,
            2u,
            3u,

            4u,
            5u,
            6u,
            4u,
            6u,
            7u,

            8u,
            9u,
            10u,
            8u,
            10u,
            11u
        };

        return NavMeshBuilder::build_from_triangles(
            vertices,
            indices,
            a_outAsset);
    }
}
