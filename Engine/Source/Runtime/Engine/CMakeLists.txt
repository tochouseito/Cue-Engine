find_package(nlohmann_json CONFIG REQUIRED)

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
    "GpuData/Transform.h"
    "GpuData/Batching.h"
    "Passes/GenerateVisibleList.h"
    "Passes/VisibleObjectBucketizePass.h"
    "Passes/MaterialBufferCopyPass.h"
    "Passes/RenderableInfoCopyPass.h"
    "Passes/TransformBufferCopyPass.h"
    "Passes/UploadBufferCopyPass.h"
    "Passes/StaticMeshBatchingPass.h"
    "Passes/StaticMeshForwardPass.h"
    "Asset/ModelAssetFormat.h"
    "Asset/TextureAssetFormat.h"
    "Asset/AssetManager.h"
    "Asset/AssetManager.cpp" "GameCore/Components.h" "Commands.h" "GameCore/Systems/RenderableObjectSystem.h" "GameCore/WorldResources.h" "GameCore/WorldResources.cpp" "GpuData/ViewProjection.h" "GameCore/Systems/CameraSystem.h" "GameCore/GameWorld.h" "GameCore/SceneAsset.h" "GameCore/GameObject.h" "GameCore/SceneInstance.h" "GameCore/GameCoreTypes.h" "GameCore/GameObjectProto.h" "GameCore/SceneSerializer.h" "GameCore/SceneSerializer.cpp" "GameCore/GameWorld.cpp")

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
    nlohmann_json::nlohmann_json
)

# インクルードディレクトリの追加
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(Engine PRIVATE
    CUE_PROJECT_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(Engine PRIVATE /FS)
endif()
