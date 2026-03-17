# windows platform の CMakeLists.txt

# スタティックライブラリを作成
add_library(win_platform STATIC "win_platform.h" "win_platform.cpp")

# リンクするライブラリの指定
target_link_libraries(win_platform PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    PAL
)

# インクルードディレクトリの追加
target_include_directories(win_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
