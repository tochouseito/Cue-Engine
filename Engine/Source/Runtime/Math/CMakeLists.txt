# Math モジュールの CMakeLists.txt

# ターゲットソースの追加
add_library(Math STATIC
    "CueMath.cpp"
    "CueMath.h"
    "Math_pch.h"
    "Math_pch.cpp"
)

# リンクするライブラリの指定
target_link_libraries(Math PUBLIC
    cue_warnings
    cue_compile_options
    Base
)

# インクルードディレクトリの指定
target_include_directories(Math PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# プリコンパイラヘッダの指定
target_precompile_headers(Math PRIVATE "Math_pch.h")
