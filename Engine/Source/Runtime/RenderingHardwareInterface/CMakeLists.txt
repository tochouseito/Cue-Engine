# RHI モジュールの CMakeLists.txt

# ターゲットソースファイルの追加
add_library(RHI STATIC "RHI.h" "RHI.cpp" "RHICommon.h" "ShaderCompiler.h" "TextureManager.h" "ViewManager.h" "BufferManager.h" "SlotUploader.h" "PipelineManager.h")

# リンクするライブラリの指定
target_link_libraries( RHI
  cue_warnings
  cue_compile_options
  Base
  CueMath
  Core
)

# ターゲットのインクルードディレクトリを設定
target_include_directories(RHI PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# サブディレクトリの追加
add_subdirectory("D3D12")
