# Editor モジュールの CMake 設定

find_package(directx-dxc CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ターゲットの追加
add_executable(Editor WIN32
    "App/Editor.cpp"
    "ImGui/ImGuiManager.h"
    "ImGui/ImGuiManager.cpp"
    "Views/Icon.h"
    "Views/Statistics.h"
    "Views/DebugView.h"
    "Ui/EditorManager.h"
    "Ui/EditorManager.cpp"
    "Ui/Hierarchy.h"
    "Ui/Inspector.h"
    "Ui/ProjectHub.h"
    "Ui/ProjectHub.cpp"
    "Project/ProjectGenerator.h"
    "Project/ProjectGenerator.cpp"
    "Build/BuildSystem.h"
    "Build/BuildSystem.cpp"
    "Build/IBuildRunner.h"
    "Build/CMakeBuildRunner.h"
    "Build/CMakeBuildRunner.cpp"
    "Build/VisualStudioBridge.h"
    "Build/VisualStudioBridge.cpp"
    "Build/VisualStudioBuildRunner.h"
    "Build/VisualStudioBuildRunner.cpp"
)

# リンクするライブラリの指定
target_link_libraries(Editor PRIVATE
    cue_warnings
    cue_compile_options
    Base
    win_platform
    d3d12_backend
    xaudio2_backend
    Engine
    imgui::imgui
    nlohmann_json::nlohmann_json
)

# インクルードディレクトリの追加
target_include_directories(Editor PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/App"
    "${CMAKE_CURRENT_SOURCE_DIR}/Ui"
    "${CMAKE_CURRENT_SOURCE_DIR}/Project"
    "${CMAKE_CURRENT_SOURCE_DIR}/Build"
    "${CMAKE_CURRENT_SOURCE_DIR}/ImGui"
    "${CMAKE_CURRENT_SOURCE_DIR}/Views"
)

target_compile_definitions(Editor PRIVATE
    CUE_PROJECT_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

set_target_properties(Editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/Editor/$<CONFIG>"
)

# サブディレクトリの追加

# ビルド後のコマンドを追加して、必要な DLL をコピー
add_custom_command(
    TARGET Editor
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/dxcompiler.dll"
            "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/bin/dxil.dll"
            "$<TARGET_FILE_DIR:Editor>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Shaders"
            "$<TARGET_FILE_DIR:Editor>/EngineResources/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Fonts"
            "$<TARGET_FILE_DIR:Editor>/EngineResources/Fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:Editor>/config"
    VERBATIM
)
