find_package(nlohmann_json CONFIG REQUIRED)
find_package(recastnavigation CONFIG REQUIRED)
find_package(Freetype REQUIRED)

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
    "DrawSystem/DebugViewShadingMode.h"
    "DrawSystem/FontAtlasManager.h"
    "DrawSystem/FontAtlasManager.cpp"
    "DrawSystem/StaticMeshPool.h"
    "DrawSystem/StaticMeshPool.cpp"
    "DrawSystem/StaticMeshPoolTypes.h"
    "LightingSystem/GpuData/LightData.h"
    "LightingSystem/LightingBindings.h"
    "LightingSystem/LightCollector.h"
    "LightingSystem/LightFrameState.h"
    "LightingSystem/LightResources.h"
    "LightingSystem/LightResources.cpp"
    "LightingSystem/LightScene.h"
    "LightingSystem/Passes/LightBufferCopyPass.h"
    "LightingSystem/Systems/LightSystem.h"
    "ShadowSystem/GpuData/ShadowData.h"
    "ShadowSystem/ShadowAtlasAllocator.h"
    "ShadowSystem/ShadowBindings.h"
    "ShadowSystem/ShadowCollector.h"
    "ShadowSystem/ShadowFrameState.h"
    "ShadowSystem/ShadowResources.h"
    "ShadowSystem/ShadowResources.cpp"
    "ShadowSystem/ShadowScene.h"
    "ShadowSystem/Passes/DirectionalShadowMapPass.h"
    "ShadowSystem/Passes/PointShadowMapPass.h"
    "ShadowSystem/Passes/ShadowBufferCopyPass.h"
    "ShadowSystem/Passes/SpotShadowMapPass.h"
    "ShadowSystem/Passes/SpotShadowMapPreviewPass.h"
    "ShadowSystem/Systems/ShadowSystem.h"
    "ParticleSystem/ParticleFrameState.h"
    "ParticleSystem/ParticleResources.h"
    "ParticleSystem/ParticleResources.cpp"
    "ParticleSystem/ParticleScene.h"
    "ParticleSystem/Passes/ParticleBufferCopyPass.h"
    "ParticleSystem/Passes/ParticleInitializePass.h"
    "ParticleSystem/Passes/ParticleSpawnPass.h"
    "ParticleSystem/Passes/ParticleSpriteForwardPass.h"
    "ParticleSystem/Passes/ParticleUpdatePass.h"
    "ParticleSystem/Systems/ParticleEmitterSystem.h"
    "GpuData/Transform.h"
    "GpuData/Batching.h"
    "GpuData/Sprite.h"
    "GpuData/Particle.h"
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
    "DrawSystem/Passes/SkinPaletteBufferCopyPass.h"
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
    "DrawSystem/Passes/SkyboxPass.h"
    "Asset/ModelAssetFormat.h"
    "Asset/SoundAssetFormat.h"
    "Asset/TextureAssetFormat.h"
    "Asset/AssetManager.h"
    "Asset/AssetManager.cpp"
    "AnimationSystem/AnimationSystem.h"
    "GameCore/Components.h"
    "Commands.h"
    "DrawSystem/Systems/RenderableObjectSystem.h"
    "DrawSystem/Systems/SkinnedRenderableObjectSystem.h"
    "DrawSystem/Systems/SpriteSystem.h"
    "DrawSystem/Systems/TextSystem.h"
    "GameCore/Systems/DemoEnemySystem.h"
    "GameCore/Systems/FirstPersonCameraControllerSystem.h"
    "GameCore/Systems/CharacterControllerSystem.h"
    "GameCore/Systems/PhysicsBodySystem.h"
    "GameCore/Systems/TriggerVolumeSystem.h"
    "GameCore/Systems/UiWidgetSystem.h"
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
    Freetype::Freetype
)

# インクルードディレクトリの追加
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(Engine PRIVATE
    CUE_PROJECT_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(Engine PRIVATE /FS /bigobj)
endif()
