add_library (Math STATIC "CueMath.cpp" "CueMath.h"  "TimeUnit.h" "Vector2.h" "Vector3.h" "Vector4.h" "Matrix4.h" "MathStructAllowedList.h")

target_link_libraries(Math PRIVATE Base)
target_link_libraries(Math PRIVATE cue_warnings)
target_link_libraries(Math PRIVATE cue_compile_options)

target_include_directories(Math PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# プリコンパイラヘッダ
target_precompile_headers(Math PRIVATE "math_pch.h")
