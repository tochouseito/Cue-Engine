# Engine モジュールの CMakeLists

# ターゲットソースの追加
add_library(Engine STATIC
    "Engine.h"
    "Engine.cpp"
 "Command/PlatformCommandContext.h" "Command/PlatformCommandContext.cpp" "FrameController.h" "FrameController.cpp" "DrawSystem/passes/FinalColorClearPass.h" "DrawSystem/passes/MeshForwardPass.h" "DrawSystem/passes/ObjectOcclusionDepthPass.h" "DrawSystem/passes/ObjectCullAndLodPass.h" "DrawSystem/passes/BuildHiZDepthPass.h" "DrawSystem/passes/CellCullingPass.h" "DrawSystem/passes/ClusteredLightingPass.h" "DrawSystem/passes/PresentToSwapChain.h" "DrawSystem/passes/DrawResourceCopyPasses.h" "DrawSystem/passes/GenerateVisibleListPass.h" "DrawSystem/passes/StaticMeshBatchingPass.h" "DrawSystem/passes/OccluderDepthOnlyIndirectPass.h" "DrawSystem/passes/StaticMeshForwardPass.h" "DrawSystem/MeshPool.h" "DrawSystem/MeshPool.cpp" "DrawSystem/DrawResources.h" "DrawSystem/DrawResources.cpp" "DrawSystem/DrawFrameState.h" "GpuData/Batching.h" "GpuData/ClusteredLighting.h" "GpuData/Transform.h" "GpuData/ViewProjection.h" "LightingSystem/GpuData/LightData.h" "LightingSystem/LightingBindings.h" "LightingSystem/LightResources.h" "LightingSystem/LightResources.cpp" "LightingSystem/Passes/LightBufferCopyPass.h")

# リンクするライブラリの指定
target_link_libraries(Engine PUBLIC
    cue_warnings
    cue_compile_options
    Base
    CueMath
    Core
    PAL
    RHI
)

# インクルードディレクトリの指定
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(Engine PRIVATE /FS /bigobj)
endif()
