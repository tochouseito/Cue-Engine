#include "RecastNavigationBackend.h"

// === C++ includes ===
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

// === ThirdParty includes ===
#include <recastnavigation/DetourAlloc.h>
#include <recastnavigation/DetourNavMesh.h>
#include <recastnavigation/DetourNavMeshBuilder.h>
#include <recastnavigation/DetourNavMeshQuery.h>
#include <recastnavigation/DetourStatus.h>
#include <recastnavigation/Recast.h>

namespace Cue::GameCore
{
    namespace
    {
        constexpr uint64_t k_fnvOffset = 14695981039346656037ull;
        constexpr uint64_t k_fnvPrime = 1099511628211ull;
        constexpr uint16_t k_defaultNavPolyFlags = 0x1u;

        template <typename T, void (*FreeFunc)(T*)>
        struct RecastDeleter final
        {
            void operator()(T* a_ptr) const noexcept
            {
                if (a_ptr != nullptr)
                {
                    FreeFunc(a_ptr);
                }
            }
        };

        using HeightfieldPtr =
            std::unique_ptr<rcHeightfield,
                RecastDeleter<rcHeightfield, rcFreeHeightField>>;
        using CompactHeightfieldPtr =
            std::unique_ptr<rcCompactHeightfield,
                RecastDeleter<rcCompactHeightfield, rcFreeCompactHeightfield>>;
        using ContourSetPtr =
            std::unique_ptr<rcContourSet,
                RecastDeleter<rcContourSet, rcFreeContourSet>>;
        using PolyMeshPtr =
            std::unique_ptr<rcPolyMesh,
                RecastDeleter<rcPolyMesh, rcFreePolyMesh>>;
        using PolyMeshDetailPtr =
            std::unique_ptr<rcPolyMeshDetail,
                RecastDeleter<rcPolyMeshDetail, rcFreePolyMeshDetail>>;

        struct DetourDataDeleter final
        {
            void operator()(unsigned char* a_ptr) const noexcept
            {
                if (a_ptr != nullptr)
                {
                    dtFree(a_ptr);
                }
            }
        };

        using DetourDataPtr =
            std::unique_ptr<unsigned char, DetourDataDeleter>;

        struct DetourNavMeshDeleter final
        {
            void operator()(dtNavMesh* a_ptr) const noexcept
            {
                if (a_ptr != nullptr)
                {
                    dtFreeNavMesh(a_ptr);
                }
            }
        };

        struct DetourNavMeshQueryDeleter final
        {
            void operator()(dtNavMeshQuery* a_ptr) const noexcept
            {
                if (a_ptr != nullptr)
                {
                    dtFreeNavMeshQuery(a_ptr);
                }
            }
        };

        using NavMeshPtr =
            std::unique_ptr<dtNavMesh, DetourNavMeshDeleter>;
        using NavMeshQueryPtr =
            std::unique_ptr<dtNavMeshQuery, DetourNavMeshQueryDeleter>;

        constexpr int k_maxQueryPolys = 256;
        constexpr int k_maxStraightPathPoints = 256;

        void hash_bytes(uint64_t& a_hash, const void* a_data, size_t a_size) noexcept
        {
            const auto* bytes = static_cast<const unsigned char*>(a_data);
            for (size_t byteIndex = 0; byteIndex < a_size; ++byteIndex)
            {
                a_hash ^= bytes[byteIndex];
                a_hash *= k_fnvPrime;
            }
        }

        void hash_triangle(uint64_t& a_hash,
            const NavMeshTriangle& a_triangle) noexcept
        {
            hash_bytes(a_hash, &a_triangle.v0, sizeof(a_triangle.v0));
            hash_bytes(a_hash, &a_triangle.v1, sizeof(a_triangle.v1));
            hash_bytes(a_hash, &a_triangle.v2, sizeof(a_triangle.v2));
            hash_bytes(a_hash, &a_triangle.area, sizeof(a_triangle.area));
        }

        [[nodiscard]] bool is_valid_settings(
            const NavMeshBakeSettings& a_settings) noexcept
        {
            return a_settings.cellSize > 0.0f &&
                a_settings.cellHeight > 0.0f &&
                a_settings.agentHeight > 0.0f &&
                a_settings.agentRadius >= 0.0f &&
                a_settings.agentMaxClimb >= 0.0f &&
                a_settings.agentMaxSlope >= 0.0f &&
                a_settings.agentMaxSlope < 90.0f &&
                a_settings.regionMinSize >= 0.0f &&
                a_settings.regionMergeSize >= 0.0f &&
                a_settings.edgeMaxLen >= 0.0f &&
                a_settings.edgeMaxError >= 0.0f &&
                a_settings.vertsPerPoly >= 3 &&
                a_settings.detailSampleDist >= 0.0f &&
                a_settings.detailSampleMaxError >= 0.0f;
        }

        [[nodiscard]] int voxel_count(float a_value, float a_cellSize) noexcept
        {
            return static_cast<int>(std::ceil(a_value / a_cellSize));
        }

        [[nodiscard]] int voxel_floor_count(
            float a_value, float a_cellSize) noexcept
        {
            return static_cast<int>(std::floor(a_value / a_cellSize));
        }

        [[nodiscard]] unsigned char to_recast_area(uint8_t a_area) noexcept
        {
            const uint8_t clampedArea = (std::min)(a_area,
                static_cast<uint8_t>(k_maxNavAreaCount - 2u));
            return static_cast<unsigned char>(clampedArea + 1u);
        }

        [[nodiscard]] unsigned char to_detour_area(
            unsigned char a_recastArea) noexcept
        {
            if (a_recastArea == RC_NULL_AREA)
            {
                return RC_NULL_AREA;
            }
            if (a_recastArea == RC_WALKABLE_AREA)
            {
                return static_cast<unsigned char>(NavAreaType::Walkable);
            }

            return static_cast<unsigned char>((std::min)(
                static_cast<int>(a_recastArea - 1u),
                static_cast<int>(k_maxNavAreaCount - 1u)));
        }

        [[nodiscard]] uint64_t hash_input(
            const NavMeshBuildInput& a_input) noexcept
        {
            uint64_t hash = k_fnvOffset;
            for (const NavMeshTriangle& triangle : a_input.triangles)
            {
                hash_triangle(hash, triangle);
            }

            return hash;
        }

        [[nodiscard]] uint64_t hash_build(
            uint64_t a_sourceGeometryHash,
            const NavMeshBakeSettings& a_settings) noexcept
        {
            uint64_t hash = k_fnvOffset;
            hash_bytes(hash, &a_sourceGeometryHash, sizeof(a_sourceGeometryHash));
            hash_bytes(hash, &a_settings.cellSize, sizeof(a_settings.cellSize));
            hash_bytes(hash, &a_settings.cellHeight, sizeof(a_settings.cellHeight));
            hash_bytes(hash, &a_settings.agentHeight, sizeof(a_settings.agentHeight));
            hash_bytes(hash, &a_settings.agentRadius, sizeof(a_settings.agentRadius));
            hash_bytes(hash, &a_settings.agentMaxClimb,
                sizeof(a_settings.agentMaxClimb));
            hash_bytes(hash, &a_settings.agentMaxSlope,
                sizeof(a_settings.agentMaxSlope));
            hash_bytes(hash, &a_settings.regionMinSize,
                sizeof(a_settings.regionMinSize));
            hash_bytes(hash, &a_settings.regionMergeSize,
                sizeof(a_settings.regionMergeSize));
            hash_bytes(hash, &a_settings.edgeMaxLen, sizeof(a_settings.edgeMaxLen));
            hash_bytes(hash, &a_settings.edgeMaxError,
                sizeof(a_settings.edgeMaxError));
            hash_bytes(hash, &a_settings.detailSampleDist,
                sizeof(a_settings.detailSampleDist));
            hash_bytes(hash, &a_settings.detailSampleMaxError,
                sizeof(a_settings.detailSampleMaxError));
            hash_bytes(hash, &a_settings.vertsPerPoly,
                sizeof(a_settings.vertsPerPoly));
            return hash;
        }

        [[nodiscard]] Result build_recast_config(
            const NavMeshBuildInput& a_input,
            const NavMeshBakeSettings& a_settings,
            rcConfig& a_outConfig) noexcept
        {
            if (!is_valid_settings(a_settings))
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Navigation bake settings are invalid.");
            }
            if (a_input.triangles.empty())
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Navigation bake input has no triangles.");
            }

            a_outConfig = {};
            a_outConfig.cs = a_settings.cellSize;
            a_outConfig.ch = a_settings.cellHeight;
            a_outConfig.walkableSlopeAngle = a_settings.agentMaxSlope;
            a_outConfig.walkableHeight =
                voxel_count(a_settings.agentHeight, a_settings.cellHeight);
            a_outConfig.walkableClimb =
                voxel_floor_count(a_settings.agentMaxClimb, a_settings.cellHeight);
            a_outConfig.walkableRadius =
                voxel_count(a_settings.agentRadius, a_settings.cellSize);
            a_outConfig.maxEdgeLen =
                voxel_count(a_settings.edgeMaxLen, a_settings.cellSize);
            a_outConfig.maxSimplificationError = a_settings.edgeMaxError;
            a_outConfig.minRegionArea = static_cast<int>(
                a_settings.regionMinSize * a_settings.regionMinSize);
            a_outConfig.mergeRegionArea = static_cast<int>(
                a_settings.regionMergeSize * a_settings.regionMergeSize);
            a_outConfig.maxVertsPerPoly = a_settings.vertsPerPoly;
            a_outConfig.detailSampleDist = a_settings.detailSampleDist > 0.0f
                ? a_settings.cellSize * a_settings.detailSampleDist
                : 0.0f;
            a_outConfig.detailSampleMaxError =
                a_settings.cellHeight * a_settings.detailSampleMaxError;

            a_outConfig.bmin[0] = (std::numeric_limits<float>::max)();
            a_outConfig.bmin[1] = (std::numeric_limits<float>::max)();
            a_outConfig.bmin[2] = (std::numeric_limits<float>::max)();
            a_outConfig.bmax[0] = (std::numeric_limits<float>::lowest)();
            a_outConfig.bmax[1] = (std::numeric_limits<float>::lowest)();
            a_outConfig.bmax[2] = (std::numeric_limits<float>::lowest)();

            for (const NavMeshTriangle& triangle : a_input.triangles)
            {
                const Math::float3 vertices[3] = {
                    triangle.v0,
                    triangle.v1,
                    triangle.v2,
                };
                for (const Math::float3& vertex : vertices)
                {
                    a_outConfig.bmin[0] =
                        (std::min)(a_outConfig.bmin[0], vertex.x);
                    a_outConfig.bmin[1] =
                        (std::min)(a_outConfig.bmin[1], vertex.y);
                    a_outConfig.bmin[2] =
                        (std::min)(a_outConfig.bmin[2], vertex.z);
                    a_outConfig.bmax[0] =
                        (std::max)(a_outConfig.bmax[0], vertex.x);
                    a_outConfig.bmax[1] =
                        (std::max)(a_outConfig.bmax[1], vertex.y);
                    a_outConfig.bmax[2] =
                        (std::max)(a_outConfig.bmax[2], vertex.z);
                }
            }

            rcCalcGridSize(a_outConfig.bmin, a_outConfig.bmax, a_outConfig.cs,
                &a_outConfig.width, &a_outConfig.height);
            if (a_outConfig.width <= 0 || a_outConfig.height <= 0)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Navigation bake input bounds are invalid.");
            }

            return Result::ok();
        }

        void build_vertex_and_index_buffers(const NavMeshBuildInput& a_input,
            std::vector<float>& a_outVertices,
            std::vector<int>& a_outIndices,
            std::vector<unsigned char>& a_outAreas)
        {
            a_outVertices.clear();
            a_outIndices.clear();
            a_outAreas.clear();
            a_outVertices.reserve(a_input.triangles.size() * 9u);
            a_outIndices.reserve(a_input.triangles.size() * 3u);
            a_outAreas.reserve(a_input.triangles.size());

            int nextIndex = 0;
            for (const NavMeshTriangle& triangle : a_input.triangles)
            {
                const Math::float3 vertices[3] = {
                    triangle.v0,
                    triangle.v1,
                    triangle.v2,
                };

                for (const Math::float3& vertex : vertices)
                {
                    a_outVertices.push_back(vertex.x);
                    a_outVertices.push_back(vertex.y);
                    a_outVertices.push_back(vertex.z);
                    a_outIndices.push_back(nextIndex);
                    ++nextIndex;
                }

                a_outAreas.push_back(to_recast_area(triangle.area));
            }
        }

        void convert_poly_areas_and_flags(rcPolyMesh& a_polyMesh) noexcept
        {
            for (int polyIndex = 0; polyIndex < a_polyMesh.npolys; ++polyIndex)
            {
                a_polyMesh.areas[polyIndex] =
                    to_detour_area(a_polyMesh.areas[polyIndex]);
                a_polyMesh.flags[polyIndex] = k_defaultNavPolyFlags;
            }
        }

        void to_detour_position(
            const Math::float3& a_position, float (&a_outPosition)[3]) noexcept
        {
            a_outPosition[0] = a_position.x;
            a_outPosition[1] = a_position.y;
            a_outPosition[2] = a_position.z;
        }

        [[nodiscard]] Math::float3 from_detour_position(
            const float* a_position) noexcept
        {
            return Math::float3(a_position[0], a_position[1], a_position[2]);
        }

        [[nodiscard]] std::array<float, 3> make_query_extents(
            const NavMeshBakeSettings& a_settings) noexcept
        {
            const float radius = (std::max)(a_settings.agentRadius * 2.0f, 0.5f);
            const float height = (std::max)(a_settings.agentHeight, 1.0f);
            return { radius, height, radius };
        }

        void configure_filter(
            const NavQueryFilter& a_source,
            dtQueryFilter& a_outFilter) noexcept
        {
            a_outFilter.setIncludeFlags(a_source.includeFlags);
            a_outFilter.setExcludeFlags(a_source.excludeFlags);

            const size_t areaCount =
                (std::min)(a_source.areaCosts.size(), static_cast<size_t>(DT_MAX_AREAS));
            for (size_t areaIndex = 0; areaIndex < areaCount; ++areaIndex)
            {
                a_outFilter.setAreaCost(static_cast<int>(areaIndex),
                    (std::max)(a_source.areaCosts[areaIndex], 0.0f));
            }
        }

        [[nodiscard]] Math::float3 tile_vertex(
            const dtMeshTile& a_tile,
            int a_vertexIndex) noexcept
        {
            return from_detour_position(&a_tile.verts[a_vertexIndex * 3]);
        }

        [[nodiscard]] Math::float3 detail_vertex(
            const dtMeshTile& a_tile,
            const dtPoly& a_poly,
            const dtPolyDetail& a_detail,
            unsigned char a_index) noexcept
        {
            if (a_index < a_poly.vertCount)
            {
                return tile_vertex(a_tile, a_poly.verts[a_index]);
            }

            const int detailIndex =
                a_detail.vertBase + static_cast<int>(a_index - a_poly.vertCount);
            return from_detour_position(&a_tile.detailVerts[detailIndex * 3]);
        }
    }

    struct RecastNavigationBackend::Impl final
    {
        struct LoadedNavMesh final
        {
            NavMeshPtr navMesh = nullptr;
            NavMeshQueryPtr query = nullptr;
            std::array<float, 3> queryExtents{};
            uint32_t generation = 1;
            bool isOccupied = false;
        };

        [[nodiscard]] LoadedNavMesh* find(NavMeshHandle a_handle) noexcept
        {
            if (!a_handle.valid() || a_handle.index >= slots.size())
            {
                return nullptr;
            }

            LoadedNavMesh& slot = slots[a_handle.index];
            if (!slot.isOccupied || slot.generation != a_handle.generation)
            {
                return nullptr;
            }

            return &slot;
        }

        [[nodiscard]] NavMeshHandle add(
            NavMeshPtr a_navMesh,
            NavMeshQueryPtr a_query,
            const NavMeshBakeSettings& a_settings)
        {
            for (uint32_t slotIndex = 0;
                 slotIndex < static_cast<uint32_t>(slots.size()); ++slotIndex)
            {
                LoadedNavMesh& slot = slots[slotIndex];
                if (slot.isOccupied)
                {
                    continue;
                }

                slot.navMesh = std::move(a_navMesh);
                slot.query = std::move(a_query);
                slot.queryExtents = make_query_extents(a_settings);
                slot.isOccupied = true;
                if (slot.generation == 0)
                {
                    slot.generation = 1;
                }
                return NavMeshHandle{ slotIndex, slot.generation };
            }

            LoadedNavMesh slot{};
            slot.navMesh = std::move(a_navMesh);
            slot.query = std::move(a_query);
            slot.queryExtents = make_query_extents(a_settings);
            slot.isOccupied = true;
            slots.push_back(std::move(slot));

            return NavMeshHandle{
                static_cast<uint32_t>(slots.size() - 1u),
                slots.back().generation
            };
        }

        std::vector<LoadedNavMesh> slots{};
    };

    RecastNavigationBackend::RecastNavigationBackend()
        : m_impl(std::make_unique<Impl>())
    {}

    RecastNavigationBackend::~RecastNavigationBackend() = default;

    Result RecastNavigationBackend::bake_nav_mesh(
        const NavMeshBuildInput& a_input,
        const NavMeshBakeSettings& a_settings,
        NavMeshAssetData& a_outAsset) noexcept
    {
        a_outAsset = {};

        rcConfig config{};
        Result result = build_recast_config(a_input, a_settings, config);
        if (!result)
        {
            return result;
        }

        rcContext context{};
        std::vector<float> vertices{};
        std::vector<int> indices{};
        std::vector<unsigned char> areas{};
        build_vertex_and_index_buffers(a_input, vertices, indices, areas);

        HeightfieldPtr heightfield(rcAllocHeightfield());
        if (heightfield == nullptr)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error,
                "Recast heightfield allocation failed.");
        }
        if (!rcCreateHeightfield(&context, *heightfield, config.width,
            config.height, config.bmin, config.bmax, config.cs, config.ch))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast heightfield creation failed.");
        }

        const int vertexCount = static_cast<int>(vertices.size() / 3u);
        const int triangleCount = static_cast<int>(a_input.triangles.size());
        rcClearUnwalkableTriangles(&context, config.walkableSlopeAngle,
            vertices.data(), vertexCount, indices.data(), triangleCount,
            areas.data());
        if (!rcRasterizeTriangles(&context, vertices.data(), vertexCount,
            indices.data(), areas.data(), triangleCount, *heightfield,
            config.walkableClimb))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast triangle rasterization failed.");
        }

        rcFilterLowHangingWalkableObstacles(
            &context, config.walkableClimb, *heightfield);
        rcFilterLedgeSpans(
            &context, config.walkableHeight, config.walkableClimb, *heightfield);
        rcFilterWalkableLowHeightSpans(
            &context, config.walkableHeight, *heightfield);

        CompactHeightfieldPtr compactHeightfield(rcAllocCompactHeightfield());
        if (compactHeightfield == nullptr)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error,
                "Recast compact heightfield allocation failed.");
        }
        if (!rcBuildCompactHeightfield(&context, config.walkableHeight,
            config.walkableClimb, *heightfield, *compactHeightfield))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast compact heightfield build failed.");
        }

        if (config.walkableRadius > 0 &&
            !rcErodeWalkableArea(
                &context, config.walkableRadius, *compactHeightfield))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast walkable area erosion failed.");
        }
        if (!rcBuildDistanceField(&context, *compactHeightfield))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast distance field build failed.");
        }
        if (!rcBuildRegions(&context, *compactHeightfield, config.borderSize,
            config.minRegionArea, config.mergeRegionArea))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast region build failed.");
        }

        ContourSetPtr contourSet(rcAllocContourSet());
        if (contourSet == nullptr)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error,
                "Recast contour set allocation failed.");
        }
        if (!rcBuildContours(&context, *compactHeightfield,
            config.maxSimplificationError, config.maxEdgeLen, *contourSet))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast contour build failed.");
        }

        PolyMeshPtr polyMesh(rcAllocPolyMesh());
        if (polyMesh == nullptr)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error,
                "Recast poly mesh allocation failed.");
        }
        if (!rcBuildPolyMesh(
            &context, *contourSet, config.maxVertsPerPoly, *polyMesh))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast poly mesh build failed.");
        }

        PolyMeshDetailPtr detailMesh(rcAllocPolyMeshDetail());
        if (detailMesh == nullptr)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error,
                "Recast detail mesh allocation failed.");
        }
        if (!rcBuildPolyMeshDetail(&context, *polyMesh, *compactHeightfield,
            config.detailSampleDist, config.detailSampleMaxError, *detailMesh))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Recast detail mesh build failed.");
        }

        convert_poly_areas_and_flags(*polyMesh);

        dtNavMeshCreateParams params{};
        params.verts = polyMesh->verts;
        params.vertCount = polyMesh->nverts;
        params.polys = polyMesh->polys;
        params.polyAreas = polyMesh->areas;
        params.polyFlags = polyMesh->flags;
        params.polyCount = polyMesh->npolys;
        params.nvp = polyMesh->nvp;
        params.detailMeshes = detailMesh->meshes;
        params.detailVerts = detailMesh->verts;
        params.detailVertsCount = detailMesh->nverts;
        params.detailTris = detailMesh->tris;
        params.detailTriCount = detailMesh->ntris;
        params.walkableHeight = a_settings.agentHeight;
        params.walkableRadius = a_settings.agentRadius;
        params.walkableClimb = a_settings.agentMaxClimb;
        params.cs = config.cs;
        params.ch = config.ch;
        params.buildBvTree = true;
        std::memcpy(params.bmin, polyMesh->bmin, sizeof(params.bmin));
        std::memcpy(params.bmax, polyMesh->bmax, sizeof(params.bmax));

        unsigned char* rawNavData = nullptr;
        int rawNavDataSize = 0;
        if (!dtCreateNavMeshData(&params, &rawNavData, &rawNavDataSize) ||
            rawNavData == nullptr || rawNavDataSize <= 0)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Detour navmesh data creation failed.");
        }

        DetourDataPtr navData(rawNavData);
        const uint64_t sourceGeometryHash = a_input.sourceGeometryHash != 0
            ? a_input.sourceGeometryHash
            : hash_input(a_input);
        a_outAsset.bakeSettings = a_settings;
        a_outAsset.sourceGeometryHash = sourceGeometryHash;
        a_outAsset.buildHash = hash_build(sourceGeometryHash, a_settings);
        a_outAsset.isTiled = false;
        a_outAsset.navData.resize(static_cast<size_t>(rawNavDataSize));
        std::memcpy(a_outAsset.navData.data(), navData.get(),
            static_cast<size_t>(rawNavDataSize));

        return Result::ok();
    }

    Result RecastNavigationBackend::load_nav_mesh(
        const NavMeshAssetData& a_asset,
        NavMeshHandle& a_outHandle) noexcept
    {
        a_outHandle = {};
        if (m_impl == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Recast navigation backend is not initialized.");
        }
        if (a_asset.navData.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh asset payload must not be empty.");
        }
        if (a_asset.isTiled)
        {
            return Result::fail(Code::Unsupported, Severity::Error,
                "Tiled NavMesh assets are not supported yet.");
        }
        if (a_asset.navData.size() >
            static_cast<size_t>((std::numeric_limits<int>::max)()))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh asset payload is too large.");
        }

        auto* navData = static_cast<unsigned char*>(
            dtAlloc(static_cast<int>(a_asset.navData.size()), DT_ALLOC_PERM));
        if (navData == nullptr)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error,
                "Detour navmesh data allocation failed.");
        }
        std::memcpy(navData, a_asset.navData.data(), a_asset.navData.size());

        NavMeshPtr navMesh(dtAllocNavMesh());
        if (navMesh == nullptr)
        {
            dtFree(navData);
            return Result::fail(Code::OutOfMemory, Severity::Error,
                "Detour navmesh allocation failed.");
        }

        dtStatus status = navMesh->init(navData,
            static_cast<int>(a_asset.navData.size()), DT_TILE_FREE_DATA);
        if (dtStatusFailed(status))
        {
            dtFree(navData);
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Detour navmesh initialization failed.");
        }

        NavMeshQueryPtr query(dtAllocNavMeshQuery());
        if (query == nullptr)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error,
                "Detour navmesh query allocation failed.");
        }

        status = query->init(navMesh.get(), k_maxQueryPolys);
        if (dtStatusFailed(status))
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Detour navmesh query initialization failed.");
        }

        a_outHandle = m_impl->add(
            std::move(navMesh), std::move(query), a_asset.bakeSettings);
        return Result::ok();
    }

    Result RecastNavigationBackend::unload_nav_mesh(
        NavMeshHandle a_handle) noexcept
    {
        if (m_impl == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Recast navigation backend is not initialized.");
        }

        Impl::LoadedNavMesh* slot = m_impl->find(a_handle);
        if (slot == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Warning,
                "NavMesh handle is invalid.");
        }

        slot->query.reset();
        slot->navMesh.reset();
        slot->isOccupied = false;
        ++slot->generation;
        if (slot->generation == 0)
        {
            slot->generation = 1;
        }
        return Result::ok();
    }

    Result RecastNavigationBackend::find_nearest_point(NavMeshHandle a_handle,
        const Math::float3& a_point,
        Math::float3& a_outPoint) noexcept
    {
        a_outPoint = Math::float3::zero();
        if (m_impl == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Recast navigation backend is not initialized.");
        }

        Impl::LoadedNavMesh* slot = m_impl->find(a_handle);
        if (slot == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh handle is invalid.");
        }

        float point[3]{};
        float nearestPoint[3]{};
        dtPolyRef nearestRef = 0;
        dtQueryFilter filter{};
        to_detour_position(a_point, point);

        const dtStatus status = slot->query->findNearestPoly(point,
            slot->queryExtents.data(), &filter, &nearestRef, nearestPoint);
        if (dtStatusFailed(status) || nearestRef == 0)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Nearest navmesh polygon was not found.");
        }

        a_outPoint = from_detour_position(nearestPoint);
        return Result::ok();
    }

    Result RecastNavigationBackend::find_path(NavMeshHandle a_handle,
        const Math::float3& a_start,
        const Math::float3& a_goal,
        const NavQueryFilter& a_filter,
        NavPath& a_outPath) noexcept
    {
        a_outPath = {};
        if (m_impl == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Recast navigation backend is not initialized.");
        }

        Impl::LoadedNavMesh* slot = m_impl->find(a_handle);
        if (slot == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh handle is invalid.");
        }

        dtQueryFilter filter{};
        configure_filter(a_filter, filter);

        float start[3]{};
        float goal[3]{};
        float nearestStart[3]{};
        float nearestGoal[3]{};
        dtPolyRef startRef = 0;
        dtPolyRef goalRef = 0;
        to_detour_position(a_start, start);
        to_detour_position(a_goal, goal);

        dtStatus status = slot->query->findNearestPoly(start,
            slot->queryExtents.data(), &filter, &startRef, nearestStart);
        if (dtStatusFailed(status) || startRef == 0)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Start navmesh polygon was not found.");
        }

        status = slot->query->findNearestPoly(goal, slot->queryExtents.data(),
            &filter, &goalRef, nearestGoal);
        if (dtStatusFailed(status) || goalRef == 0)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Goal navmesh polygon was not found.");
        }

        std::array<dtPolyRef, k_maxQueryPolys> polys{};
        int polyCount = 0;
        status = slot->query->findPath(startRef, goalRef, nearestStart,
            nearestGoal, &filter, polys.data(), &polyCount, k_maxQueryPolys);
        if (dtStatusFailed(status) || polyCount <= 0)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "NavMesh path was not found.");
        }

        std::array<float, k_maxStraightPathPoints * 3> straightPoints{};
        std::array<unsigned char, k_maxStraightPathPoints> straightFlags{};
        std::array<dtPolyRef, k_maxStraightPathPoints> straightPolys{};
        int straightCount = 0;
        status = slot->query->findStraightPath(nearestStart, nearestGoal,
            polys.data(), polyCount, straightPoints.data(), straightFlags.data(),
            straightPolys.data(), &straightCount, k_maxStraightPathPoints);
        if (dtStatusFailed(status) || straightCount <= 0)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Straight navmesh path was not found.");
        }

        a_outPath.points.reserve(static_cast<size_t>(straightCount));
        for (int pointIndex = 0; pointIndex < straightCount; ++pointIndex)
        {
            a_outPath.points.push_back(
                from_detour_position(&straightPoints[pointIndex * 3]));
        }
        a_outPath.isPartial = polys[static_cast<size_t>(polyCount - 1)] != goalRef;
        return Result::ok();
    }

    Result RecastNavigationBackend::raycast(NavMeshHandle a_handle,
        const Math::float3& a_start,
        const Math::float3& a_end,
        const NavQueryFilter& a_filter,
        NavRaycastHit& a_outHit) noexcept
    {
        a_outHit = {};
        if (m_impl == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Recast navigation backend is not initialized.");
        }

        Impl::LoadedNavMesh* slot = m_impl->find(a_handle);
        if (slot == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh handle is invalid.");
        }

        dtQueryFilter filter{};
        configure_filter(a_filter, filter);

        float start[3]{};
        float end[3]{};
        float nearestStart[3]{};
        dtPolyRef startRef = 0;
        to_detour_position(a_start, start);
        to_detour_position(a_end, end);

        dtStatus status = slot->query->findNearestPoly(start,
            slot->queryExtents.data(), &filter, &startRef, nearestStart);
        if (dtStatusFailed(status) || startRef == 0)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Raycast start navmesh polygon was not found.");
        }

        float t = 0.0f;
        float hitNormal[3]{};
        std::array<dtPolyRef, k_maxQueryPolys> polys{};
        int polyCount = 0;
        status = slot->query->raycast(startRef, nearestStart, end, &filter, &t,
            hitNormal, polys.data(), &polyCount, k_maxQueryPolys);
        if (dtStatusFailed(status))
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "NavMesh raycast failed.");
        }

        const Math::float3 delta = a_end - a_start;
        const float distance = delta.length();
        const bool hasHit = std::isfinite(t) && t <= 1.0f;
        a_outHit.hasHit = hasHit;
        a_outHit.distance = hasHit ? distance * (std::max)(t, 0.0f) : distance;
        a_outHit.position = hasHit
            ? a_start + delta * (std::max)(t, 0.0f)
            : a_end;
        a_outHit.normal = hasHit
            ? from_detour_position(hitNormal)
            : Math::float3::zero();
        return Result::ok();
    }

    Result RecastNavigationBackend::build_debug_geometry(NavMeshHandle a_handle,
        NavMeshDebugGeometry& a_outGeometry) noexcept
    {
        a_outGeometry = {};
        if (m_impl == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Recast navigation backend is not initialized.");
        }

        Impl::LoadedNavMesh* slot = m_impl->find(a_handle);
        if (slot == nullptr || slot->navMesh == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "NavMesh handle is invalid.");
        }

        const dtNavMesh* navMesh = slot->navMesh.get();
        const int maxTiles = navMesh->getMaxTiles();
        for (int tileIndex = 0; tileIndex < maxTiles; ++tileIndex)
        {
            const dtMeshTile* tile = navMesh->getTile(tileIndex);
            if (tile == nullptr || tile->header == nullptr)
            {
                continue;
            }

            for (int polyIndex = 0; polyIndex < tile->header->polyCount;
                 ++polyIndex)
            {
                const dtPoly& poly = tile->polys[polyIndex];
                if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                {
                    continue;
                }

                const uint8_t area = static_cast<uint8_t>(poly.getArea());
                for (int vertexIndex = 0; vertexIndex < poly.vertCount;
                     ++vertexIndex)
                {
                    const int nextIndex =
                        (vertexIndex + 1) % static_cast<int>(poly.vertCount);
                    NavDebugLine line{};
                    line.start = tile_vertex(*tile, poly.verts[vertexIndex]);
                    line.end = tile_vertex(*tile, poly.verts[nextIndex]);
                    line.area = area;
                    a_outGeometry.polygonEdges.push_back(line);
                }

                if (tile->detailMeshes == nullptr || tile->detailTris == nullptr)
                {
                    continue;
                }

                const dtPolyDetail& detail = tile->detailMeshes[polyIndex];
                for (int triangleIndex = 0; triangleIndex < detail.triCount;
                     ++triangleIndex)
                {
                    const unsigned char* tri = &tile->detailTris[
                        (detail.triBase + triangleIndex) * 4];
                    NavDebugTriangle triangle{};
                    triangle.v0 = detail_vertex(*tile, poly, detail, tri[0]);
                    triangle.v1 = detail_vertex(*tile, poly, detail, tri[1]);
                    triangle.v2 = detail_vertex(*tile, poly, detail, tri[2]);
                    triangle.area = area;
                    a_outGeometry.triangles.push_back(triangle);
                }
            }
        }

        return Result::ok();
    }
}
