# Editor モジュールの CMake 設定

# ターゲットの追加
add_executable(Editor "Editor.cpp")

# リンクするライブラリの指定
target_link_libraries(Editor PRIVATE
    cue_warnings
    cue_compile_options
    Base
)

# インクルードディレクトリの追加
target_include_directories(Editor PRIVATE
    "${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/Base"
)

# サブディレクトリの追加
