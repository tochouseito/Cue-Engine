add_library(win_platform STATIC
    "win_platform.h"
    "win_platform.cpp"
    "../PlatformFactory.h"
    "private/WinApp.h"
    "private/WinApp.cpp"
    "win_pch.h" "win_pch.cpp"
 "private/WinQpcClock.h" "private/WinQpcClock.cpp")

# リンクライブラリ
target_link_libraries(win_platform PUBLIC Platform)
target_link_libraries(win_platform PRIVATE cue_warnings)
target_link_libraries(win_platform PRIVATE cue_compile_options)

# インクルードディレクトリ
target_include_directories(win_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(win_platform PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")

# マクロ
target_compile_definitions(win_platform PUBLIC
    PLATFORM_WIN
)

# プリコンパイラヘッダ
target_precompile_headers(win_platform PRIVATE "win_pch.h")
