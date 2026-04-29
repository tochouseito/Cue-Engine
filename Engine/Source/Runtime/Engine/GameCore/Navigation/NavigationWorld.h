#pragma once

// === Base includes ===
#include <Result.h>

// === Engine includes ===
#include "NavTypes.h"

// === C++ includes ===
#include <memory>

namespace Cue::GameCore
{
    class INavMeshBackend
    {
    public:
        virtual ~INavMeshBackend() = default;

        virtual Result bake_nav_mesh(const NavMeshBuildInput& a_input,
            const NavMeshBakeSettings& a_settings,
            NavMeshAssetData& a_outAsset) noexcept = 0;
        virtual Result load_nav_mesh(
            const NavMeshAssetData& a_asset,
            NavMeshHandle& a_outHandle) noexcept = 0;
        virtual Result unload_nav_mesh(NavMeshHandle a_handle) noexcept = 0;
        virtual Result find_nearest_point(NavMeshHandle a_handle,
            const Math::float3& a_point,
            Math::float3& a_outPoint) noexcept = 0;
        virtual Result find_path(NavMeshHandle a_handle,
            const Math::float3& a_start,
            const Math::float3& a_goal,
            const NavQueryFilter& a_filter,
            NavPath& a_outPath) noexcept = 0;
        virtual Result raycast(NavMeshHandle a_handle,
            const Math::float3& a_start,
            const Math::float3& a_end,
            const NavQueryFilter& a_filter,
            NavRaycastHit& a_outHit) noexcept = 0;
        virtual Result build_debug_geometry(NavMeshHandle a_handle,
            NavMeshDebugGeometry& a_outGeometry) noexcept = 0;
    };

    class NavigationWorld final
    {
    public:
        NavigationWorld() = default;
        ~NavigationWorld();
        NavigationWorld(const NavigationWorld&) = delete;
        NavigationWorld& operator=(const NavigationWorld&) = delete;
        NavigationWorld(NavigationWorld&&) noexcept;
        NavigationWorld& operator=(NavigationWorld&&) noexcept;

        [[nodiscard]] Result set_backend(
            std::unique_ptr<INavMeshBackend> a_backend) noexcept;
        void clear_backend() noexcept;

        [[nodiscard]] INavMeshBackend* backend() noexcept
        {
            return m_backend.get();
        }

        [[nodiscard]] const INavMeshBackend* backend() const noexcept
        {
            return m_backend.get();
        }

        [[nodiscard]] Result bake_nav_mesh(const NavMeshBuildInput& a_input,
            const NavMeshBakeSettings& a_settings,
            NavMeshAssetData& a_outAsset) noexcept;
        [[nodiscard]] Result load_nav_mesh(const NavMeshAssetData& a_asset,
            NavMeshHandle& a_outHandle) noexcept;
        [[nodiscard]] Result unload_nav_mesh(NavMeshHandle a_handle) noexcept;
        [[nodiscard]] Result find_nearest_point(NavMeshHandle a_handle,
            const Math::float3& a_point,
            Math::float3& a_outPoint) noexcept;
        [[nodiscard]] Result find_path(NavMeshHandle a_handle,
            const Math::float3& a_start,
            const Math::float3& a_goal,
            const NavQueryFilter& a_filter,
            NavPath& a_outPath) noexcept;
        [[nodiscard]] Result raycast(NavMeshHandle a_handle,
            const Math::float3& a_start,
            const Math::float3& a_end,
            const NavQueryFilter& a_filter,
            NavRaycastHit& a_outHit) noexcept;
        [[nodiscard]] Result build_debug_geometry(NavMeshHandle a_handle,
            NavMeshDebugGeometry& a_outGeometry) noexcept;

    private:
        [[nodiscard]] Result require_backend() const noexcept;

        std::unique_ptr<INavMeshBackend> m_backend = nullptr;
    };
}
