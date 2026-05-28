# windows platform モジュールの CMakeLists.txt

# ターゲットソースの追加
add_library(win_platform STATIC
    "win_platform.h"
    "win_platform.cpp"
 "stdafx.h" "ConvertHresult.h" "ConvertUTF.h" "App/WinApp.h" "App/WinApp.cpp")

# リンクするライブラリの指定
target_link_libraries(win_platform PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    PAL
)
#  固有リンク
target_link_libraries(win_platform PUBLIC
    winmm
    ole32
    shell32
    uuid
)

# インクルードディレクトリの指定
target_include_directories(win_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# MSVC でビルドする場合、/FS オプションを追加して、複数のソースファイルが同時にプリコンパイルされたヘッダーファイルを使用できるようにする
if (MSVC)
    target_compile_options(win_platform PRIVATE /FS)
endif()
