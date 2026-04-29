#pragma once

// === Core includes ===
#include <IO/IFileSystem.h>

// === Engine includes ===
#include <Asset/AssetManager.h>
#include <GameCore/Components.h>
#include "NavTypes.h"
#include "NavigationWorld.h"

// === C++ includes ===
#include <span>
#include <string_view>

namespace Cue::GameCore
{
    class NavigationBakePipeline final
    {
    public:
        NavigationBakePipeline() = delete;

        [[nodiscard]] static Result bake_model(
            AssetManager& a_assetManager,
            std::string_view a_modelName,
            const ECS::TransformComponent& a_transform,
            const NavMeshBakeSettings& a_settings,
            NavMeshAssetData& a_outAsset) noexcept;

        [[nodiscard]] static Result bake_model_to_file(
            AssetManager& a_assetManager,
            Core::IO::IFileSystem& a_fileSystem,
            std::string_view a_modelName,
            const ECS::TransformComponent& a_transform,
            const NavMeshBakeSettings& a_settings,
            const Core::IO::Path& a_outputPath,
            NavMeshAssetData& a_outAsset) noexcept;

        [[nodiscard]] static Result bake_model_to_world(
            AssetManager& a_assetManager,
            NavigationWorld& a_navigationWorld,
            std::string_view a_modelName,
            const ECS::TransformComponent& a_transform,
            const NavMeshBakeSettings& a_settings,
            NavMeshHandle& a_outHandle) noexcept;

        [[nodiscard]] static Result bake_model_to_file_and_world(
            AssetManager& a_assetManager,
            Core::IO::IFileSystem& a_fileSystem,
            NavigationWorld& a_navigationWorld,
            std::string_view a_modelName,
            const ECS::TransformComponent& a_transform,
            const NavMeshBakeSettings& a_settings,
            const Core::IO::Path& a_outputPath,
            NavMeshAssetData& a_outAsset,
            NavMeshHandle& a_outHandle) noexcept;

        [[nodiscard]] static Result bake_entities_to_file_and_world(
            ECS::ECSManager& a_ecs,
            AssetManager& a_assetManager,
            Core::IO::IFileSystem& a_fileSystem,
            NavigationWorld& a_navigationWorld,
            std::span<const ECS::Entity> a_entities,
            const NavMeshBakeSettings& a_settings,
            const Core::IO::Path& a_outputPath,
            NavMeshAssetData& a_outAsset,
            NavMeshHandle& a_outHandle) noexcept;
    };
}
