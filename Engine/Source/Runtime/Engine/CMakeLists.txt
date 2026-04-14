# Engine の CMakeLists.txt

# Static ライブラリを作成
add_library(Engine STATIC
    "Engine.h"
    "Engine.cpp"
    "FrameController.h"
    "FrameController.cpp"
    "GpuData/Transform.h"
    "GpuData/Batching.h"
    "Passes/GenerateVisibleList.h"
    "Passes/ObjectInfoCopyPass.h"
    "Passes/TransformBufferCopyPass.h"
    "Passes/UploadBufferCopyPass.h"
    "Passes/StaticMeshBatchingPass.h"
    "Passes/StaticMeshForwardPass.h"
    "Asset/AssetManager.h"
    "Asset/AssetManager.cpp" "GameCore/Components.h" "Commands.h"  "GameCore/Systems/TransformSystem.h" "GameCore/Systems/ObjectInfoSystem.h" "GameCore/WorldResources.h" "GameCore/WorldResources.cpp" "GpuData/ViewProjection.h" "GameCore/Systems/CameraSystem.h" "GameCore/GameWorld.h" "GameCore/SceneAsset.h" "GameCore/GameObject.h" "GameCore/SceneInstance.h" "GameCore/GameCoreTypes.h" "GameCore/GameObjectProto.h")

# リンクするライブラリの指定
target_link_libraries(Engine PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    ECS
    PAL
    RHI
)

# インクルードディレクトリの追加
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
