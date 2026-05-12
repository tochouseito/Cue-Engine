find_package(nlohmann_json CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)

# Engine の CMakeLists.txt

# Static ライブラリを作成
add_library(Engine STATIC
    "Engine.h"
    "Engine.cpp"
    "EngineCommandContext.h"
    "EngineCommandContext.cpp"
    "PlatformCommandContext.h"
    "PlatformCommandContext.cpp"
    "Script/ScriptModule.h"
    "Script/ScriptModule.cpp"
    "Script/ScriptModuleHost.h"
    "Script/ScriptModuleHost.cpp"
    "Script/MarionnetteObject.h"
    "Script/MarionnetteObject.cpp"
    "Script/Marionnette.h"
    "Script/Marionnette.cpp"
    "Script/ScriptRuntime.h"
    "Script/ScriptRuntime.cpp"
    "Script/ScriptShadowCopyService.h"
    "Script/ScriptShadowCopyService.cpp"
    "FrameController.h"
    "FrameController.cpp"
    "DrawSystem/DrawCollector.h"
    "DrawSystem/DrawScene.h"
    "DrawSystem/DrawFrameState.h"
    "DrawSystem/DrawResources.h"
    "DrawSystem/DrawResources.cpp"
    "DrawSystem/StaticMeshPool.h"
    "DrawSystem/StaticMeshPool.cpp"
    "DrawSystem/StaticMeshPoolTypes.h"
    "GpuData/Transform.h"
    "GpuData/Batching.h"
    "GpuData/Sprite.h"
    "GpuData/DebugPick.h"
    "GpuData/DebugSelection.h"
    "GameCore/DebugDraw.h"
    "DrawSystem/Passes/DebugDrawPass.h"
    "DrawSystem/Passes/DebugGridPass.h"
    "DrawSystem/Passes/DebugObjectIdPass.h"
    "DrawSystem/Passes/DebugOutlinePass.h"
    "DrawSystem/Passes/DebugPickReadbackPass.h"
    "DrawSystem/Passes/DebugSelectionPass.h"
    "DrawSystem/Passes/GenerateVisibleList.h"
    "DrawSystem/Passes/VisibleObjectBucketizePass.h"
    "DrawSystem/Passes/MaterialBufferCopyPass.h"
    "DrawSystem/Passes/RenderableInfoCopyPass.h"
    "DrawSystem/Passes/RenderObjectCopyPass.h"
    "DrawSystem/Passes/TransformBufferCopyPass.h"
    "DrawSystem/Passes/UploadBufferCopyPass.h"
    "DrawSystem/Passes/ViewProjectionCopyPass.h"
    "DrawSystem/Passes/VisibleObjectCountCopyPass.h"
    "DrawSystem/Passes/SpriteInstanceCopyPass.h"
    "DrawSystem/Passes/SpriteForwardPass.h"
    "DrawSystem/Passes/StaticMeshBatchingPass.h"
    "DrawSystem/Passes/StaticMeshForwardPass.h"
    "Asset/ModelAssetFormat.h"
    "Asset/SoundAssetFormat.h"
    "Asset/TextureAssetFormat.h"
    "Asset/AssetManager.h"
    "Asset/AssetManager.cpp"
    "GameCore/Components.h"
    "Commands.h"
    "DrawSystem/Systems/RenderableObjectSystem.h"
    "DrawSystem/Systems/SpriteSystem.h"
    "GameCore/Systems/DemoEnemySystem.h"
    "GameCore/Systems/FirstPersonCameraControllerSystem.h"
    "GameCore/Systems/CharacterControllerSystem.h"
    "GameCore/Systems/PhysicsBodySystem.h"
    "GameCore/Systems/TriggerVolumeSystem.h"
    "GpuData/ViewProjection.h"
    "DrawSystem/Systems/CameraSystem.h"
    "GameCore/GameWorld.h"
    "GameCore/SceneAsset.h"
    "GameCore/GameObject.h"
    "GameCore/SceneInstance.h"
    "GameCore/GameCoreTypes.h"
    "GameCore/GameObjectProto.h"
    "GameCore/SceneSerializer.h"
    "GameCore/SceneSerializer.cpp"
    "GameCore/GameWorld.cpp"
    "GameCore/Navigation/NavComponents.h"
    "GameCore/Navigation/NavAgentMotorSystem.h"
    "GameCore/Navigation/NavMeshAssetFormat.h"
    "GameCore/Navigation/NavMeshAssetSerializer.h"
    "GameCore/Navigation/NavMeshAssetSerializer.cpp"
    "GameCore/Navigation/NavTypes.h"
    "GameCore/Navigation/Navigation.h"
    "GameCore/Navigation/NavigationBakePipeline.h"
    "GameCore/Navigation/NavigationBakePipeline.cpp"
    "GameCore/Navigation/NavigationGeometryCollector.h"
    "GameCore/Navigation/NavigationGeometryCollector.cpp"
    "GameCore/Navigation/NavigationSystem.h"
    "GameCore/Navigation/NavigationWorld.h"
    "GameCore/Navigation/NavigationWorld.cpp"
    "GameCore/Navigation/RecastNavigationBackend.h"
    "GameCore/Navigation/RecastNavigationBackend.cpp")

# リンクするライブラリの指定
target_link_libraries(Engine PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Audio
    Core
    ECS
    PAL
    RHI
    Physics
    nlohmann_json::nlohmann_json
    RecastNavigation::Detour
    RecastNavigation::Recast
)

# インクルードディレクトリの追加
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(Engine PRIVATE
    CUE_PROJECT_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(Engine PRIVATE /FS /bigobj)
endif()
