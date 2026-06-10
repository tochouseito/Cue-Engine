# RendererTest モジュールの CMakeLists.txt

# パッケージを検索
find_package(assimp CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(winpixevent CONFIG REQUIRED)

# ターゲットソースの追加
add_executable(RendererTest WIN32
    "App/RendererTest.cpp"
    "App/DebugCamera.h"
    "App/DebugCamera.cpp"
 "Asset/ModelImporter.h" "Asset/ModelImporter.cpp")

# リンクするライブラリの指定
target_link_libraries(RendererTest PRIVATE
   cue_warnings
   cue_compile_options
   Base
   Core
   win_platform
   d3d12_backend
   Engine
)

target_link_libraries(RendererTest PRIVATE
    assimp::assimp
    imgui::imgui
    meshoptimizer::meshoptimizer
    nlohmann_json::nlohmann_json
    msacm32
)

# インクルードディレクトリの指定
target_include_directories(RendererTest PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# コンパイルオプション
target_compile_options(RendererTest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
)

# 生成物の出力先（ターゲット固有）
set_target_properties(RendererTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/generated/outputs/RendererTest/$<CONFIG>"
)

# ビルド後のコマンドを追加して、必要な DLL をコピー
add_custom_command(
    TARGET RendererTest
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -f
            "$<TARGET_FILE_DIR:RendererTest>/assimp-vc145-mt.dll"
            "$<TARGET_FILE_DIR:RendererTest>/assimp-vc145-mtd.dll"
            "$<TARGET_FILE_DIR:RendererTest>/kubazip.dll"
            "$<TARGET_FILE_DIR:RendererTest>/minizip.dll"
            "$<TARGET_FILE_DIR:RendererTest>/poly2tri.dll"
            "$<TARGET_FILE_DIR:RendererTest>/pugixml.dll"
            "$<TARGET_FILE_DIR:RendererTest>/zlib1.dll"
            "$<TARGET_FILE_DIR:RendererTest>/zlibd1.dll"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CUE_VCPKG_HOST_BIN_DIR}/dxcompiler.dll"
            "${CUE_VCPKG_HOST_BIN_DIR}/dxil.dll"
            "$<TARGET_FILE:Microsoft::WinPixEventRuntime>"
            "$<TARGET_FILE_DIR:RendererTest>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Shaders"
            "$<TARGET_FILE_DIR:RendererTest>/EngineResources/Shaders"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Fonts"
            "$<TARGET_FILE_DIR:RendererTest>/EngineResources/Fonts"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/Engine/Textures"
            "$<TARGET_FILE_DIR:RendererTest>/EngineResources/Textures"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:RendererTest>/config"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/TestProject/Assets"
            "$<TARGET_FILE_DIR:RendererTest>/TestProject/Assets"
    VERBATIM
)
