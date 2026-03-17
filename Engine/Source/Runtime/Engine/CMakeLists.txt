# Engine の CMakeLists.txt

# Static ライブラリを作成
add_library(Engine STATIC "Engine.h" "Engine.cpp")

# リンクするライブラリの指定
target_link_libraries(Engine PUBLIC
    cue_warnings
    cue_compile_options
    Base
    Core
    PAL
    RHI
)

# インクルードディレクトリの追加
target_include_directories(Engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
