# 構成の種類
add_library(d3d12_backend STATIC "d3d12_backend.h" "d3d12_backend.cpp")

# リンクライブラリ
target_link_libraries(d3d12_backend PRIVATE cue_warnings)
target_link_libraries(d3d12_backend PRIVATE cue_compile_options)
target_link_libraries(d3d12_backend PRIVATE GraphicsCore)

# インクルードディレクトリ
target_include_directories(d3d12_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# マクロ
target_compile_definitions(d3d12_backend PUBLIC
    BACKEND_DX12
)
