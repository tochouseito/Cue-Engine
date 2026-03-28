# Rendering Hardware Interface

# スタティックライブラリを作成
add_library(RHI STATIC "RHI.cpp" "RHI.h" "BackendFactory.h" "FrameGraph.h" "FrameGraph.cpp" "Interfaces.h" "RHICommon.h" "ShaderCompiler.h" "BufferManager.h" "TextureManager.h" "PipelineManager.h" "ViewManager.h" "SlotUploader.h")

# リンクするライブラリの指定
target_link_libraries(RHI PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
)

# インクルードディレクトリの追加
target_include_directories(RHI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# サブディレクトリの追加
add_subdirectory("d3d12")
