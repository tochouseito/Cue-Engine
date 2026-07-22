# Editor モジュールの CMakeLists.txt

# パッケージを検索
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(winpixevent CONFIG REQUIRED)

# ターゲットソースの追加
add_executable(Editor WIN32
    "App/Editor.cpp"
    "App/EditorResources.rc" "DebugCamera.h" "DebugCamera.cpp" "EditorManager.h" "EditorManager.cpp" "Scene/EditorSceneManager.h" "Scene/EditorSceneManager.cpp" "Workspace/AssetBrowser.h" "Workspace/AssetBrowser.cpp" "Workspace/AssetSelection.h" "Workspace/AssetSelection.cpp" "Workspace/Hierarchy.h" "Workspace/Hierarchy.cpp" "Workspace/Inspector.h" "Workspace/Inspector.cpp" "Project/EditorProject.h" "Project/EditorProject.cpp" "Project/GameProjectGenerator.h" "Project/GameProjectGenerator.cpp" "Project/GameScriptBuildRunner.h" "Project/GameScriptBuildRunner.cpp" "Project/ProjectSelector.h" "Project/ProjectSelector.cpp" "Project/ProjectSettings.h" "Project/ProjectSettings.cpp" "Project/VisualStudioLauncher.h" "Project/VisualStudioLauncher.cpp" "ImGuiManager/ImGuiManager.h" "ImGuiManager/ImGuiManager.cpp" "Workspace/DebugView.h"  "Workspace/DebugView.cpp" "Workspace/GameView.h" "Workspace/GameView.cpp")

target_sources(Editor PRIVATE
    "App/EditorMcpBridge.h"
    "App/EditorMcpBridge.cpp"
)

# リンクするライブラリの指定
target_link_libraries(Editor PRIVATE
   cue_warnings
   cue_compile_options
   Base
   Core
   win_platform
   d3d12_backend
   Engine
)

target_link_libraries(Editor PRIVATE
    assimp::assimp
    imgui::imgui
    imguizmo::imguizmo
    meshoptimizer::meshoptimizer
    nlohmann_json::nlohmann_json
    msacm32
    advapi32
    ws2_32
)

# インクルードディレクトリの指定
target_include_directories(Editor PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# 定数定義
target_compile_definitions(Editor PRIVATE
    CUE_EDITOR_ROOT_PATH=L"${CMAKE_SOURCE_DIR}"
    $<$<CONFIG:Debug>:CUE_EDITOR_DEBUG_LAYOUT_MIRROR_PATH="${CMAKE_SOURCE_DIR}/config/editor/imgui.ini">
)

# コンパイルオプション
target_compile_options(Editor PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)

# 生成物の出力先（ターゲット固有）
set_target_properties(Editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/Editor/$<CONFIG>"
)

# ビルド後のコマンドを追加して、必要な DLL をコピー
add_custom_command(
    TARGET Editor
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -f
            "$<TARGET_FILE_DIR:Editor>/assimp-vc145-mt.dll"
            "$<TARGET_FILE_DIR:Editor>/assimp-vc145-mtd.dll"
            "$<TARGET_FILE_DIR:Editor>/kubazip.dll"
            "$<TARGET_FILE_DIR:Editor>/minizip.dll"
            "$<TARGET_FILE_DIR:Editor>/poly2tri.dll"
            "$<TARGET_FILE_DIR:Editor>/pugixml.dll"
            "$<TARGET_FILE_DIR:Editor>/zlib1.dll"
            "$<TARGET_FILE_DIR:Editor>/zlibd1.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CUE_VCPKG_HOST_BIN_DIR}/dxcompiler.dll"
            "${CUE_VCPKG_HOST_BIN_DIR}/dxil.dll"
            "$<TARGET_FILE:Microsoft::WinPixEventRuntime>"
            "$<TARGET_FILE_DIR:Editor>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Shaders"
            "$<TARGET_FILE_DIR:Editor>/EngineResources/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Fonts"
            "$<TARGET_FILE_DIR:Editor>/EngineResources/Fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Textures"
            "$<TARGET_FILE_DIR:Editor>/EngineResources/Textures"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:Editor>/config"
    VERBATIM
)
