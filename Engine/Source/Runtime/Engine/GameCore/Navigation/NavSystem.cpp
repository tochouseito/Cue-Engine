// === Engine includes ===
#include "GameCore/Navigation/NavSystem.h"

#include "GameCore/Navigation/NavFunnel.h"
#include "GameCore/Navigation/NavPathFinder.h"

// === C++ includes ===
#include <vector>

namespace Cue::GameCore
{
    namespace
    {
        [[nodiscard]] NavPolyId resolve_poly(
            const NavMeshAsset& a_asset,
            const NavSampleHit& a_hit,
            float a_maxHeightDistance) noexcept
        {
            NavPolyId polyId = NavMeshQuery::find_containing_poly(
                a_asset,
                a_hit.position,
                a_maxHeightDistance);

            if (polyId == k_invalidNavPolyId)
            {
                polyId = a_hit.polyId;
            }

            return polyId;
        }
    }

    void NavSystem::set_asset(const NavMeshAsset* a_asset) noexcept
    {
        m_asset = a_asset;
    }

    const NavMeshAsset* NavSystem::get_asset() const noexcept
    {
        return m_asset;
    }

    bool NavSystem::has_asset() const noexcept
    {
        return m_asset != nullptr;
    }

    bool NavSystem::find_path(
        const Math::float3& a_start,
        const Math::float3& a_goal,
        const NavAgentSettings& a_agent,
        NavPath& a_outPath) const
    {
        a_outPath.points.clear();

        if (m_asset == nullptr || a_agent.maxSnapHeight < 0.0f)
        {
            return false;
        }

        NavSampleHit startHit{};
        NavSampleHit goalHit{};
        if (!NavMeshQuery::sample_position(
                *m_asset,
                a_start,
                a_agent.maxSnapHeight,
                startHit) ||
            !NavMeshQuery::sample_position(
                *m_asset,
                a_goal,
                a_agent.maxSnapHeight,
                goalHit))
        {
            return false;
        }

        const NavPolyId startPolyId =
            resolve_poly(*m_asset, startHit, a_agent.maxSnapHeight);
        const NavPolyId goalPolyId =
            resolve_poly(*m_asset, goalHit, a_agent.maxSnapHeight);

        if (!m_asset->is_valid_poly(startPolyId) ||
            !m_asset->is_valid_poly(goalPolyId))
        {
            return false;
        }

        std::vector<NavPolyId> polyPath{};
        if (!NavPathFinder::find_poly_path(
                *m_asset,
                startPolyId,
                goalPolyId,
                polyPath))
        {
            return false;
        }

        std::vector<NavPortal> portals{};
        if (!NavFunnel::build_portal_path(*m_asset, polyPath, portals))
        {
            return false;
        }

        return NavFunnel::run(
            startHit.position,
            goalHit.position,
            portals,
            a_outPath);
    }

    bool NavSystem::sample_position(
        const Math::float3& a_position,
        float a_maxDistance,
        NavSampleHit& a_outHit) const noexcept
    {
        if (m_asset == nullptr)
        {
            return false;
        }

        return NavMeshQuery::sample_position(
            *m_asset,
            a_position,
            a_maxDistance,
            a_outHit);
    }

    bool NavSystem::is_on_nav_mesh(
        const Math::float3& a_position,
        float a_maxHeightDistance) const noexcept
    {
        if (m_asset == nullptr)
        {
            return false;
        }

        return NavMeshQuery::find_containing_poly(
            *m_asset,
            a_position,
            a_maxHeightDistance) != k_invalidNavPolyId;
    }
}
