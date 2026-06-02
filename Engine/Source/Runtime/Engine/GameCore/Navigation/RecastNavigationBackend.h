// RecastNavigationBackend の役割と公開要素を定義する

#pragma once

// === Engine includes ===
#include "NavigationWorld.h"

// === C++ includes ===
#include <memory>

namespace Cue::GameCore
{
    class RecastNavigationBackend final : public INavMeshBackend
    {
    public:
        RecastNavigationBackend();
        ~RecastNavigationBackend() override;
        RecastNavigationBackend(const RecastNavigationBackend&) = delete;
        RecastNavigationBackend& operator=(const RecastNavigationBackend&) =
            delete;
        RecastNavigationBackend(RecastNavigationBackend&&) = delete;
        RecastNavigationBackend& operator=(RecastNavigationBackend&&) = delete;

        Result bake_nav_mesh(const NavMeshBuildInput& a_input,
            const NavMeshBakeSettings& a_settings,
            NavMeshAssetData& a_outAsset) noexcept override;
        Result load_nav_mesh(const NavMeshAssetData& a_asset,
            NavMeshHandle& a_outHandle) noexcept override;
        Result unload_nav_mesh(NavMeshHandle a_handle) noexcept override;
        Result find_nearest_point(NavMeshHandle a_handle,
            const Math::float3& a_point,
            Math::float3& a_outPoint) noexcept override;
        Result find_path(NavMeshHandle a_handle,
            const Math::float3& a_start,
            const Math::float3& a_goal,
            const NavQueryFilter& a_filter,
            NavPath& a_outPath) noexcept override;
        Result raycast(NavMeshHandle a_handle,
            const Math::float3& a_start,
            const Math::float3& a_end,
            const NavQueryFilter& a_filter,
            NavRaycastHit& a_outHit) noexcept override;
        Result build_debug_geometry(NavMeshHandle a_handle,
            NavMeshDebugGeometry& a_outGeometry) noexcept override;

    private:
        struct Impl;

        std::unique_ptr<Impl> m_impl;
    };
}
