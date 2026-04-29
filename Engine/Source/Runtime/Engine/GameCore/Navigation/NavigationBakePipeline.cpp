#include "NavigationBakePipeline.h"

// === Engine includes ===
#include "NavigationGeometryCollector.h"
#include "NavMeshAssetSerializer.h"
#include "RecastNavigationBackend.h"

namespace Cue::GameCore
{
    Result NavigationBakePipeline::bake_model(
        AssetManager& a_assetManager,
        std::string_view a_modelName,
        const ECS::TransformComponent& a_transform,
        const NavMeshBakeSettings& a_settings,
        NavMeshAssetData& a_outAsset) noexcept
    {
        a_outAsset = {};

        NavMeshBuildInput input{};
        Result result = NavigationGeometryCollector::append_model(a_assetManager,
            a_modelName, a_transform,
            static_cast<uint8_t>(NavAreaType::Walkable), input);
        if (!result)
        {
            return result;
        }

        RecastNavigationBackend backend{};
        return backend.bake_nav_mesh(input, a_settings, a_outAsset);
    }

    Result NavigationBakePipeline::bake_model_to_file(
        AssetManager& a_assetManager,
        Core::IO::IFileSystem& a_fileSystem,
        std::string_view a_modelName,
        const ECS::TransformComponent& a_transform,
        const NavMeshBakeSettings& a_settings,
        const Core::IO::Path& a_outputPath,
        NavMeshAssetData& a_outAsset) noexcept
    {
        Result result = bake_model(a_assetManager, a_modelName, a_transform,
            a_settings, a_outAsset);
        if (!result)
        {
            return result;
        }

        return NavMeshAssetSerializer::save(
            a_outAsset, a_fileSystem, a_outputPath);
    }

    Result NavigationBakePipeline::bake_model_to_world(
        AssetManager& a_assetManager,
        NavigationWorld& a_navigationWorld,
        std::string_view a_modelName,
        const ECS::TransformComponent& a_transform,
        const NavMeshBakeSettings& a_settings,
        NavMeshHandle& a_outHandle) noexcept
    {
        a_outHandle = {};

        NavMeshAssetData asset{};
        Result result = bake_model(a_assetManager, a_modelName, a_transform,
            a_settings, asset);
        if (!result)
        {
            return result;
        }

        return a_navigationWorld.load_nav_mesh(asset, a_outHandle);
    }

    Result NavigationBakePipeline::bake_model_to_file_and_world(
        AssetManager& a_assetManager,
        Core::IO::IFileSystem& a_fileSystem,
        NavigationWorld& a_navigationWorld,
        std::string_view a_modelName,
        const ECS::TransformComponent& a_transform,
        const NavMeshBakeSettings& a_settings,
        const Core::IO::Path& a_outputPath,
        NavMeshAssetData& a_outAsset,
        NavMeshHandle& a_outHandle) noexcept
    {
        a_outHandle = {};

        Result result = bake_model_to_file(a_assetManager, a_fileSystem,
            a_modelName, a_transform, a_settings, a_outputPath, a_outAsset);
        if (!result)
        {
            return result;
        }

        return a_navigationWorld.load_nav_mesh(a_outAsset, a_outHandle);
    }

    Result NavigationBakePipeline::bake_entities_to_file_and_world(
        ECS::ECSManager& a_ecs,
        AssetManager& a_assetManager,
        Core::IO::IFileSystem& a_fileSystem,
        NavigationWorld& a_navigationWorld,
        std::span<const ECS::Entity> a_entities,
        const NavMeshBakeSettings& a_settings,
        const Core::IO::Path& a_outputPath,
        NavMeshAssetData& a_outAsset,
        NavMeshHandle& a_outHandle) noexcept
    {
        a_outAsset = {};
        a_outHandle = {};

        NavMeshBuildInput input{};
        Result result = NavigationGeometryCollector::collect_entities(
            a_ecs, a_assetManager, a_entities, input);
        if (!result)
        {
            return result;
        }

        result = a_navigationWorld.bake_nav_mesh(
            input, a_settings, a_outAsset);
        if (!result)
        {
            return result;
        }

        result = NavMeshAssetSerializer::save(
            a_outAsset, a_fileSystem, a_outputPath);
        if (!result)
        {
            return result;
        }

        return a_navigationWorld.load_nav_mesh(a_outAsset, a_outHandle);
    }
}
