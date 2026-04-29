#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Native/EngineNativeStruct.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <GameCore/Components.h>
#include "NavComponents.h"
#include "NavTypes.h"

// === C++ includes ===
#include <span>
#include <string_view>

namespace Cue::GameCore
{
    class NavigationGeometryCollector final
    {
    public:
        NavigationGeometryCollector() = delete;

        [[nodiscard]] static Result append_model(
            const Core::Native::ModelData& a_modelData,
            const ECS::TransformComponent& a_transform,
            uint8_t a_area,
            NavMeshBuildInput& a_outInput) noexcept;

        [[nodiscard]] static Result append_model(
            AssetManager& a_assetManager,
            std::string_view a_modelName,
            const ECS::TransformComponent& a_transform,
            uint8_t a_area,
            NavMeshBuildInput& a_outInput) noexcept;

        [[nodiscard]] static Result append_entity(
            ECS::ECSManager& a_ecs,
            AssetManager& a_assetManager,
            ECS::Entity a_entity,
            NavMeshBuildInput& a_outInput) noexcept;

        [[nodiscard]] static Result collect_entities(
            ECS::ECSManager& a_ecs,
            AssetManager& a_assetManager,
            std::span<const ECS::Entity> a_entities,
            NavMeshBuildInput& a_outInput) noexcept;
    };
}
