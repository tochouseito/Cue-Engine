# Renderer モジュールの CMakeLists.txt

# パッケージを検索
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(winpixevent CONFIG REQUIRED)

# ターゲットソースの追加
add_executable(Renderer WIN32
    "App/Renderer.cpp"
    "App/RendererResources.rc"
    "App/DebugCamera.h"
    "App/DebugCamera.cpp"
 "Asset/ModelImporter.h" "Asset/ModelImporter.cpp")

# リンクするライブラリの指定
target_link_libraries(Renderer PRIVATE
   cue_warnings
   cue_compile_options
   Base
   Core
   win_platform
   d3d12_backend
   Engine
   Xinput
)

target_link_libraries(Renderer PRIVATE
    assimp::assimp
    imgui::imgui
    imguizmo::imguizmo
    meshoptimizer::meshoptimizer
    nlohmann_json::nlohmann_json
    msacm32
)

# インクルードディレクトリの指定
target_include_directories(Renderer PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# 定数定義
target_compile_definitions(Renderer PRIVATE
    CUE_PROJECT_ROOT_PATH="${CMAKE_SOURCE_DIR}"
)

# コンパイルオプション
target_compile_options(Renderer PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)

# 生成物の出力先（ターゲット固有）
set_target_properties(Renderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/Renderer/$<CONFIG>"
)

# ビルド後のコマンドを追加して、必要な DLL をコピー
add_custom_command(
    TARGET Renderer
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -f
            "$<TARGET_FILE_DIR:Renderer>/assimp-vc145-mt.dll"
            "$<TARGET_FILE_DIR:Renderer>/assimp-vc145-mtd.dll"
            "$<TARGET_FILE_DIR:Renderer>/kubazip.dll"
            "$<TARGET_FILE_DIR:Renderer>/minizip.dll"
            "$<TARGET_FILE_DIR:Renderer>/poly2tri.dll"
            "$<TARGET_FILE_DIR:Renderer>/pugixml.dll"
            "$<TARGET_FILE_DIR:Renderer>/zlib1.dll"
            "$<TARGET_FILE_DIR:Renderer>/zlibd1.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CUE_VCPKG_HOST_BIN_DIR}/dxcompiler.dll"
            "${CUE_VCPKG_HOST_BIN_DIR}/dxil.dll"
            "$<TARGET_FILE:Microsoft::WinPixEventRuntime>"
            "$<TARGET_FILE_DIR:Renderer>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Shaders"
            "$<TARGET_FILE_DIR:Renderer>/EngineResources/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Fonts"
            "$<TARGET_FILE_DIR:Renderer>/EngineResources/Fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Textures"
            "$<TARGET_FILE_DIR:Renderer>/EngineResources/Textures"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:Renderer>/config"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/TestProject"
            "$<TARGET_FILE_DIR:Renderer>/TestProject"
    VERBATIM
)
