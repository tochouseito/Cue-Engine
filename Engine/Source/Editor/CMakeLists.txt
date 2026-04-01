# Editor モジュールの CMake 設定

find_package(directx-dxc CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# ターゲットの追加
add_executable(Editor WIN32 "Editor.cpp" "ImGuiManager.h" "ImGuiManager.cpp" "Statistics.h")

# リンクするライブラリの指定
target_link_libraries(Editor PRIVATE
    cue_warnings
    cue_compile_options
    Base
    win_platform
    d3d12_backend
    Engine
    imgui::imgui
)

# インクルードディレクトリの追加
target_include_directories(Editor PRIVATE
    "${CMAKE_SOURCE_DIR}"
)

# サブディレクトリの追加

# ビルド後のコマンドを追加して、必要な DLL をコピー
add_custom_command(
    TARGET Editor
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Microsoft::DirectXShaderCompiler>"
            "$<TARGET_FILE:Microsoft::DXIL>"
            "$<TARGET_FILE_DIR:Editor>"
    VERBATIM
)
