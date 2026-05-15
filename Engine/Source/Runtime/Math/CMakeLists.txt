# Math モジュールの CMake 設定

# スタティックライブラリの追加
add_library(CueMath STATIC
    "CueMath.cpp"
    "CueMath.h"
    "MathStructAllowedList.h"
    "Matrix4.h"
    "TimeUnit.h"
    "Vector2.h"
    "Vector3.h"
    "Vector4.h"
 "Quaternion.h")

# リンクするライブラリの指定
target_link_libraries(CueMath PUBLIC
    cue_warnings
    cue_compile_options
    Base
)

# 追加のインクルードディレクトリの指定
target_include_directories(CueMath PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# プリコンパイラヘッダの設定
target_precompile_headers(CueMath PRIVATE "Math_pch.h")
