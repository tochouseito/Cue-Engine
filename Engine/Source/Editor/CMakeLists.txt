# Editor モジュールの CMake 設定

# ターゲットの追加
add_executable(Editor "Editor.cpp")

# リンクするライブラリの指定
target_link_libraries(Editor PRIVATE
    cue_warnings
    cue_compile_options
)

# サブディレクトリの追加
