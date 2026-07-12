#pragma once

/// **********************************************************************
/// GameWorld の階層 Transform を解決する
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Engine includes ===
#include "Components.h"
#include "GameCoreTypes.h"

// === C++ includes ===
#include <cstdint>
#include <vector>

namespace Cue::GameCore
{
    class GameWorld;

    class TransformSystem final
    {
    public:
        /// @brief 親子関係を設定し、必要に応じて WorldTransform を維持する
        [[nodiscard]] static Result set_parent(
            GameWorld& a_world,
            EntityId a_childEntityId,
            EntityId a_parentEntityId,
            bool a_keepsWorldTransform) noexcept;

        /// @brief 親子関係を解除し、必要に応じて WorldTransform を維持する
        [[nodiscard]] static Result detach_parent(
            GameWorld& a_world,
            EntityId a_childEntityId,
            bool a_keepsWorldTransform) noexcept;

        /// @brief 全 Entity の WorldTransformComponent を親子関係に従って同期する
        static void sync_world_transforms(GameWorld& a_world) noexcept;

    private:
        [[nodiscard]] static ECS::WorldTransformComponent compose_world_transform(
            const ECS::WorldTransformComponent& a_parent,
            const ECS::TransformComponent& a_local) noexcept;

        [[nodiscard]] static ECS::TransformComponent make_local_transform(
            const ECS::WorldTransformComponent& a_parent,
            const ECS::WorldTransformComponent& a_world) noexcept;

        [[nodiscard]] static bool resolve_world_transform(
            GameWorld& a_world,
            EntityId a_entityId,
            std::vector<uint8_t>& a_state,
            ECS::WorldTransformComponent& a_outWorld) noexcept;
    };
} // namespace Cue::GameCore
