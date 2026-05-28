// NavigationWorld の実装点を分け、経路探索状態の所有と Recast 依存を局所化する

#include "NavigationWorld.h"

namespace Cue::GameCore
{
    NavigationWorld::~NavigationWorld() = default;
    NavigationWorld::NavigationWorld(NavigationWorld&&) noexcept = default;
    NavigationWorld& NavigationWorld::operator=(NavigationWorld&&) noexcept =
        default;

    Result NavigationWorld::set_backend(
        std::unique_ptr<INavMeshBackend> a_backend) noexcept
    {
        if (a_backend == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Navigation backend must not be null.");
        }

        m_backend = std::move(a_backend);
        return Result::ok();
    }

    void NavigationWorld::clear_backend() noexcept
    {
        m_backend.reset();
    }

    Result NavigationWorld::bake_nav_mesh(const NavMeshBuildInput& a_input,
        const NavMeshBakeSettings& a_settings,
        NavMeshAssetData& a_outAsset) noexcept
    {
        Result result = require_backend();
        if (!result)
        {
            a_outAsset = {};
            return result;
        }

        return m_backend->bake_nav_mesh(a_input, a_settings, a_outAsset);
    }

    Result NavigationWorld::load_nav_mesh(
        const NavMeshAssetData& a_asset,
        NavMeshHandle& a_outHandle) noexcept
    {
        Result result = require_backend();
        if (!result)
        {
            a_outHandle = {};
            return result;
        }

        return m_backend->load_nav_mesh(a_asset, a_outHandle);
    }

    Result NavigationWorld::unload_nav_mesh(NavMeshHandle a_handle) noexcept
    {
        Result result = require_backend();
        if (!result)
        {
            return result;
        }

        return m_backend->unload_nav_mesh(a_handle);
    }

    Result NavigationWorld::find_nearest_point(NavMeshHandle a_handle,
        const Math::float3& a_point,
        Math::float3& a_outPoint) noexcept
    {
        Result result = require_backend();
        if (!result)
        {
            a_outPoint = Math::float3::zero();
            return result;
        }

        return m_backend->find_nearest_point(a_handle, a_point, a_outPoint);
    }

    Result NavigationWorld::find_path(NavMeshHandle a_handle,
        const Math::float3& a_start,
        const Math::float3& a_goal,
        const NavQueryFilter& a_filter,
        NavPath& a_outPath) noexcept
    {
        Result result = require_backend();
        if (!result)
        {
            a_outPath = {};
            return result;
        }

        return m_backend->find_path(
            a_handle, a_start, a_goal, a_filter, a_outPath);
    }

    Result NavigationWorld::raycast(NavMeshHandle a_handle,
        const Math::float3& a_start,
        const Math::float3& a_end,
        const NavQueryFilter& a_filter,
        NavRaycastHit& a_outHit) noexcept
    {
        Result result = require_backend();
        if (!result)
        {
            a_outHit = {};
            return result;
        }

        return m_backend->raycast(
            a_handle, a_start, a_end, a_filter, a_outHit);
    }

    Result NavigationWorld::build_debug_geometry(NavMeshHandle a_handle,
        NavMeshDebugGeometry& a_outGeometry) noexcept
    {
        Result result = require_backend();
        if (!result)
        {
            a_outGeometry = {};
            return result;
        }

        return m_backend->build_debug_geometry(a_handle, a_outGeometry);
    }

    Result NavigationWorld::require_backend() const noexcept
    {
        return m_backend != nullptr
            ? Result::ok()
            : Result::fail(Code::InvalidState, Severity::Error,
                "Navigation backend is not set.");
    }
}
