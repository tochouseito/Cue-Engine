# PAL モジュールの CMakeLists.txt

# ヘッダー/設定のみのターゲット
add_library(PAL STATIC
    "PAL.h"
 "PAL.cpp" "PlatformMessage.h" "PlatformRuntimeState.h" "PlatformCommands.h")

# リンクするライブラリの指定
target_link_libraries(PAL PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
)

# インクルードディレクトリの指定
target_include_directories(PAL PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# サブディレクトリの追加
add_subdirectory("Win")
