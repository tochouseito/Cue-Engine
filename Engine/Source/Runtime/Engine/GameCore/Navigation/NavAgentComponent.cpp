// === Engine includes ===
#include "GameCore/Navigation/NavAgentComponent.h"

#include "GameCore/Navigation/NavMath.h"

namespace Cue::GameCore
{
    void NavAgentComponent::set_path(const NavPath& a_path)
    {
        m_path = a_path;
        m_currentWaypoint = 0;
    }

    void NavAgentComponent::clear_path() noexcept
    {
        m_path.points.clear();
        m_currentWaypoint = 0;
    }

    NavAgentUpdateResult NavAgentComponent::update(
        Math::float3& a_position,
        float a_deltaTime) noexcept
    {
        NavAgentUpdateResult result{};

        if (m_path.points.empty() || speed <= 0.0f || stoppingDistance < 0.0f)
        {
            result.isFinished = true;
            return result;
        }

        while (m_currentWaypoint < m_path.points.size())
        {
            const Math::float3& target = m_path.points[m_currentWaypoint];
            if (NavMath::distance(a_position, target) > stoppingDistance)
            {
                break;
            }

            ++m_currentWaypoint;
        }

        if (m_currentWaypoint >= m_path.points.size())
        {
            result.target = m_path.points.back();
            result.isFinished = true;
            return result;
        }

        const Math::float3 target = m_path.points[m_currentWaypoint];
        result.target = target;
        result.isFinished = false;

        const float maxMove = speed * a_deltaTime;
        if (maxMove <= 0.0f)
        {
            return result;
        }

        const Math::float3 toTarget = target - a_position;
        const float distance = toTarget.length();
        if (distance <= stoppingDistance)
        {
            ++m_currentWaypoint;
            result.isFinished = m_currentWaypoint >= m_path.points.size();
            return result;
        }

        if (maxMove >= distance)
        {
            a_position = target;
            ++m_currentWaypoint;
            result.didMove = true;
            result.isFinished = m_currentWaypoint >= m_path.points.size();
            return result;
        }

        a_position += toTarget / distance * maxMove;
        result.didMove = true;
        return result;
    }

    bool NavAgentComponent::has_path() const noexcept
    {
        return !m_path.points.empty();
    }

    bool NavAgentComponent::is_finished() const noexcept
    {
        return m_currentWaypoint >= m_path.points.size();
    }

    std::size_t NavAgentComponent::get_current_waypoint_index() const noexcept
    {
        return m_currentWaypoint;
    }

    const NavPath& NavAgentComponent::get_path() const noexcept
    {
        return m_path;
    }
}
