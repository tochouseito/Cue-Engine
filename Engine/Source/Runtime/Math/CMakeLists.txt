# Math モジュールの CMakeLists.txt

# ターゲットソースの追加
add_library(CueMath STATIC
    "CueMath.cpp"
    "CueMath.h"
    "MathStructAllowedList.h"
    "Math_pch.h"
    "Math_pch.cpp"
    "Matrix4.h"
    "Quaternion.h"
    "TimeUnit.h"
    "Vector2.h"
    "Vector3.h"
    "Vector4.h"
)

# リンクするライブラリの指定
target_link_libraries(CueMath PUBLIC
    cue_warnings
    cue_compile_options
    Base
)

# インクルードディレクトリの指定
target_include_directories(CueMath PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# プリコンパイラヘッダの指定
target_precompile_headers(CueMath PRIVATE "Math_pch.h")
