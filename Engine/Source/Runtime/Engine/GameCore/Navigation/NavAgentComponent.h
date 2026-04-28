#pragma once

// === Engine includes ===
#include "GameCore/Navigation/NavTypes.h"

// === C++ includes ===
#include <cstddef>

namespace Cue::GameCore
{
    // Agent の追従更新結果。
    struct NavAgentUpdateResult final
    {
        Math::float3 target = Math::float3::zero();
        bool didMove = false;
        bool isFinished = true;
    };

    // NavPath を追従する最小 Agent。
    class NavAgentComponent final
    {
    public:
        float speed = 3.0f;
        float stoppingDistance = 0.2f;

        /// @brief 追従する経路を設定します。
        void set_path(const NavPath& a_path);

        /// @brief 現在の経路を破棄します。
        void clear_path() noexcept;

        /// @brief 現在位置を経路に沿って更新します。
        [[nodiscard]] NavAgentUpdateResult update(
            Math::float3& a_position,
            float a_deltaTime) noexcept;

        /// @brief 経路が設定されているか判定します。
        [[nodiscard]] bool has_path() const noexcept;

        /// @brief 経路の終端まで到達したか判定します。
        [[nodiscard]] bool is_finished() const noexcept;

        /// @brief 現在追従中の waypoint index を返します。
        [[nodiscard]] std::size_t get_current_waypoint_index() const noexcept;

        /// @brief 現在の経路を返します。
        [[nodiscard]] const NavPath& get_path() const noexcept;

    private:
        NavPath m_path{};
        std::size_t m_currentWaypoint = 0;
    };
}
