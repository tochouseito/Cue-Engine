# 構成の設定
add_library(GraphicsCore STATIC "GraphicsCore.h" "GraphicsCore.cpp")

# リンクライブラリ
target_link_libraries(GraphicsCore PRIVATE cue_warnings)
target_link_libraries(GraphicsCore PRIVATE cue_compile_options)

# インクルードディレクトリ
target_include_directories(GraphicsCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
