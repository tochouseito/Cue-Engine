#pragma once

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Cue::GameCore
{
    inline constexpr uint32_t k_maxNavAreaCount = 64;

    struct NavMeshHandle final
    {
        static constexpr uint32_t k_invalid = UINT32_MAX;

        uint32_t index = k_invalid;
        uint32_t generation = 0;

        [[nodiscard]] bool valid() const noexcept
        {
            return index != k_invalid;
        }
    };

    enum class NavAreaType : uint8_t
    {
        Walkable,
        Mud,
        Water,
        Danger,
        Jump,
        Door,
    };

    struct NavQueryFilter final
    {
        NavQueryFilter() noexcept
        {
            areaCosts.fill(1.0f);
        }

        uint16_t includeFlags = 0xFFFFu;
        uint16_t excludeFlags = 0;
        std::array<float, k_maxNavAreaCount> areaCosts{};
    };

    struct NavPath final
    {
        std::vector<Math::float3> points{};
        bool isPartial = false;
    };

    struct NavRaycastHit final
    {
        Math::float3 position = Math::float3::zero();
        Math::float3 normal = Math::float3::zero();
        float distance = 0.0f;
        bool hasHit = false;
    };

    struct NavDebugLine final
    {
        Math::float3 start = Math::float3::zero();
        Math::float3 end = Math::float3::zero();
        uint8_t area = static_cast<uint8_t>(NavAreaType::Walkable);
    };

    struct NavDebugTriangle final
    {
        Math::float3 v0 = Math::float3::zero();
        Math::float3 v1 = Math::float3::zero();
        Math::float3 v2 = Math::float3::zero();
        uint8_t area = static_cast<uint8_t>(NavAreaType::Walkable);
    };

    struct NavMeshDebugGeometry final
    {
        std::vector<NavDebugTriangle> triangles{};
        std::vector<NavDebugLine> polygonEdges{};
        std::vector<NavDebugLine> pathLines{};
    };

    struct NavMeshBakeSettings final
    {
        float cellSize = 0.3f;
        float cellHeight = 0.2f;
        float agentHeight = 2.0f;
        float agentRadius = 0.6f;
        float agentMaxClimb = 0.9f;
        float agentMaxSlope = 45.0f;
        float regionMinSize = 8.0f;
        float regionMergeSize = 20.0f;
        float edgeMaxLen = 12.0f;
        float edgeMaxError = 1.3f;
        float detailSampleDist = 6.0f;
        float detailSampleMaxError = 1.0f;
        int32_t vertsPerPoly = 6;
    };

    struct NavMeshTriangle final
    {
        Math::float3 v0 = Math::float3::zero();
        Math::float3 v1 = Math::float3::zero();
        Math::float3 v2 = Math::float3::zero();
        uint8_t area = static_cast<uint8_t>(NavAreaType::Walkable);
    };

    struct NavMeshBuildInput final
    {
        std::vector<NavMeshTriangle> triangles{};
        uint64_t sourceGeometryHash = 0;
    };

    struct NavMeshAssetData final
    {
        NavMeshBakeSettings bakeSettings{};
        std::vector<std::byte> navData{};
        uint64_t sourceGeometryHash = 0;
        uint64_t buildHash = 0;
        bool isTiled = false;
    };
}
