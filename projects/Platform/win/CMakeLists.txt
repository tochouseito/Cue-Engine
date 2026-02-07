add_library(win_platform STATIC
    "win_platform.h"
    "win_platform.cpp"
    "private/WinApp.h"
    "private/WinApp.cpp"
    "pch.h" "pch.cpp"
)

# リンクライブラリ
target_link_libraries(win_platform PUBLIC Platform)
target_link_libraries(win_platform PRIVATE cue_warnings)
target_link_libraries(win_platform PRIVATE cue_compile_options)

# インクルードディレクトリ
target_include_directories(win_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(win_platform PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")

# プリコンパイラヘッダ
target_precompile_headers(win_platform PRIVATE "pch.h")
