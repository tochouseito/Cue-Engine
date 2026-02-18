add_library (Math STATIC "CueMath.cpp" "CueMath.h"  "TimeUnit.h")

target_link_libraries(Math PRIVATE Base)
target_link_libraries(Math PRIVATE cue_warnings)
target_link_libraries(Math PRIVATE cue_compile_options)

target_include_directories(Math PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# プリコンパイラヘッダ
target_precompile_headers(Math PRIVATE "math_pch.h")
