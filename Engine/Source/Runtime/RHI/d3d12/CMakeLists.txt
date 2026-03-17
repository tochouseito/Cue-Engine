# DirectX12 モジュールの CMakeLists.txt

# Static ライブラリを作成
add_library(d3d12_backend STATIC "d3d12_backend.h" "d3d12_backend.cpp")

# リンクするライブラリの指定
target_link_libraries(d3d12_backend PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    RHI
    win_platform
)

# インクルードディレクトリの追加
target_include_directories(d3d12_backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
