# CMakeList.txt : Core の CMake プロジェクト。ソースを含めて、次を定義します:
# プロジェクト専用ロジックはこちらです。
#

# ソースをこのプロジェクトの実行可能ファイルに追加します。
add_executable (Core "Core.cpp" "Core.h")

target_link_libraries(Core PRIVATE cue_warnings)
target_link_libraries(Core PRIVATE cue_compile_options)

# TODO: テストを追加し、必要な場合は、ターゲットをインストールします。
