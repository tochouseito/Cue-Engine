# CMakeList.txt : Core の CMake プロジェクト。ソースを含めて、次を定義します:
# プロジェクト専用ロジックはこちらです。
#

# ソースをこのプロジェクトの実行可能ファイルに追加します。
add_library (Core STATIC "Core.cpp" "Core.h" "public/Result.h" "public/Pool.h"   "public/LogAssert.h" )

target_link_libraries(Core PRIVATE cue_warnings)
target_link_libraries(Core PRIVATE cue_compile_options)

target_include_directories(Core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/public")
target_include_directories(Core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/private")

# TODO: テストを追加し、必要な場合は、ターゲットをインストールします。
