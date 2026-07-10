# Engine モジュールの CMakeLists

# パッケージを検索
find_package(nlohmann_json CONFIG REQUIRED)

# ターゲットソースの追加
add_library(Engine STATIC
    "Engine.h"
    "Engine.cpp"
 "Command/PlatformCommandContext.h" "Command/PlatformCommandContext.cpp" "FrameController.h" "FrameController.cpp" "DrawSystem/passes/PresentToSwapChain.h" "DrawSystem/passes/DrawSceneUploadCopyPass.h" "DrawSystem/passes/DrawSceneUploadCopyPass.cpp" "DrawSystem/passes/DrawViewUploadCopyPass.h" "DrawSystem/passes/DrawViewUploadCopyPass.cpp" "DrawSystem/passes/DrawVisibilityUploadCopyPass.h" "DrawSystem/passes/DrawVisibilityUploadCopyPass.cpp" "DrawSystem/passes/StaticMeshIndirectPass.h" "DrawSystem/passes/StaticMeshIndirectPass.cpp" "DrawSystem/DrawFrameState.h" "DrawSystem/DrawScene.h" "DrawSystem/DrawScene.cpp" "DrawSystem/DrawSceneResources.h" "DrawSystem/DrawSceneResources.cpp" "DrawSystem/DrawViewResources.h" "DrawSystem/DrawViewResources.cpp" "DrawSystem/DrawVisibilityResources.h" "DrawSystem/DrawVisibilityResources.cpp" "DrawSystem/RenderView.h" "DrawSystem/Systems/CameraSystem.h" "DrawSystem/Systems/RenderableObjectSystem.h" "GpuData/ViewProjection.h" "DrawSystem/MeshPool.h" "DrawSystem/MeshPool.cpp" "DrawSystem/GpuData/Batching.h" "DrawSystem/StaticMeshBatch.h" "DrawSystem/StaticMeshBatcher.h" "DrawSystem/StaticMeshBatcher.cpp" "GameCore/Components.h" "GameCore/Components.cpp" "GameCore/GameCoreTypes.h" "GameCore/ObjectSnapshot.h" "GameCore/SceneAsset.h" "GameCore/SceneSerializer.h" "GameCore/SceneSerializer.cpp" "Command/EngineCommandContext.h" "Command/EngineCommandContext.cpp" "Command/Commands.h"  "GameCore/GameWorld.h" "GameCore/GameWorld.cpp" "GameCore/GameObject.h" "GameCore/GameObject.cpp" "GameCore/GameObjectProto.h" "GameCore/GameObjectProto.cpp" "GameCore/GameWorldRenderExtractor.h" "GameCore/GameWorldRenderExtractor.cpp")
 
# リンクするライブラリの指定
target_link_libraries(Engine PUBLIC
    cue_warnings
    cue_compile_options
    Base
    CueMath
    Core
    ECS
    PAL
    RHI
)

target_link_libraries(Engine PRIVATE
    nlohmann_json::nlohmann_json
)

# インクルードディレクトリの指定
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(Engine PRIVATE /FS /bigobj)
endif()
