# 構成の設定
add_library(GraphicsCore STATIC "GraphicsCore.h" "GraphicsCore.cpp" "BackendFactory.h"   "FrameGraph.h" "FrameGraph.cpp"      "GraphicsCommon.h" "BufferManager.h"  "TextureManager.h" "PipelineManager.h" "ShaderCompiler.h"     "SlotUploader.h" "GraphicsInterface.h" "ViewManager.h" "StaticMeshBufferPool.h" "StaticMeshBufferPool.cpp" "TransformBufferPool.h" "TransformBufferPool.cpp")

# リンクライブラリ
target_link_libraries(GraphicsCore PRIVATE cue_warnings)
target_link_libraries(GraphicsCore PRIVATE cue_compile_options)
target_link_libraries(GraphicsCore PUBLIC Core)

# インクルードディレクトリ
target_include_directories(GraphicsCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
