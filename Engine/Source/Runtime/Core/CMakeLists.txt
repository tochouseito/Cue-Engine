# Core モジュールの CMakeLists.txt

# ターゲットソースの追加
add_library(Core STATIC "CQRS/CQRS.h" "CQRS/CQRS.cpp")

# リンクするライブラリの指定
target_link_libraries(Core PUBLIC
    cue_warnings
    cue_compile_options
    Base
)

# インクルードディレクトリの指定
target_include_directories(Core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
