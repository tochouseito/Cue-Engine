# Engine モジュールの CMakeLists

# ターゲットソースの追加
add_library(Engine STATIC
    "Engine.h"
    "Engine.cpp"
 "Command/PlatformCommandContext.h" "Command/PlatformCommandContext.cpp" "FrameController.h" "FrameController.cpp" "DrawSystem/passes/PresentToSwapChain.h" "DrawSystem/passes/FinalColorClearPass.h" "DrawSystem/DrawFrameState.h" "GpuData/ViewProjection.h" "DrawSystem/MeshPool.h" "DrawSystem/MeshPool.cpp" "DrawSystem/DrawResources.h" "DrawSystem/DrawResources.cpp" "DrawSystem/GpuData/Batching.h" "GameCore/Components.h" "GameCore/Components.cpp" "GameCore/GameCoreTypes.h" "Command/EngineCommandContext.h" "Command/EngineCommandContext.cpp" "Command/Commands.h"  "GameCore/GameWorld.h" "GameCore/GameWorld.cpp" "GameCore/GameObject.h" "GameCore/GameObject.cpp" "GameCore/GameObjectProto.h" "GameCore/GameObjectProto.cpp")
 
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

# インクルードディレクトリの指定
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(Engine PRIVATE /FS /bigobj)
endif()
