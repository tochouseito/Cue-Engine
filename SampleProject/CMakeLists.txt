cmake_minimum_required(VERSION 3.24)

project(GameScriptProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Project を Engine repository 外へ配置した場合も、cache または環境変数で SDK の場所を固定できるようにする。
set(CUE_ENGINE_ROOT "" CACHE PATH "CueEngine repository root")

if(CUE_ENGINE_ROOT STREQUAL "")
    # Engine と同じ repository 配下へ作成した Project は、親 directory を探索して設定不要で開けるようにする。
    set(_cue_engine_search_root "${CMAKE_CURRENT_SOURCE_DIR}")
    while(TRUE)
        if(EXISTS "${_cue_engine_search_root}/Engine/Source/Runtime/Engine/Script/ScriptModuleApi.h")
            set(CUE_ENGINE_ROOT "${_cue_engine_search_root}")
            break()
        endif()

        get_filename_component(_cue_engine_parent "${_cue_engine_search_root}" DIRECTORY)
        if(_cue_engine_parent STREQUAL _cue_engine_search_root)
            break()
        endif()

        set(_cue_engine_search_root "${_cue_engine_parent}")
    endwhile()
endif()

if(CUE_ENGINE_ROOT STREQUAL "" AND DEFINED ENV{CUE_ENGINE_ROOT})
    set(CUE_ENGINE_ROOT "$ENV{CUE_ENGINE_ROOT}")
endif()

if(NOT EXISTS "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine/Script/ScriptModuleApi.h")
    message(FATAL_ERROR
        "CueEngine root not found. Place this project under the CueEngine repository or set CUE_ENGINE_ROOT.")
endif()

file(GLOB_RECURSE GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*.cpp"
)

# Script source の追加を CMake 再構成へ反映し、Editor で生成した Asset を手作業なしで DLL に含める。
add_library(GameScript SHARED
    "EngineModule/GameScriptModule.cpp"
    ${GAME_SCRIPT_SOURCES}
)

target_include_directories(GameScript PRIVATE
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Base"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Math"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/ECS"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/PlatformAbstractionLayer"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/RenderingHardwareInterface"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine"
)

# GameScript は Engine SDK の Script ABI 実装を利用するため、同じ構成の library をリンクする。
target_link_libraries(GameScript PRIVATE
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/Engine.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/Base.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/Core.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/CueMath.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/ECS.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/PAL.lib"
    "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>/RHI.lib"
)

if(MSVC)
    # Engine の Math 型が利用する anonymous union は MSVC 拡張であり、GameScript の警告判定から分離する。
    target_compile_options(GameScript PRIVATE
        /utf-8
        /W4
        /EHsc
        /wd4201
    )
endif()

set_target_properties(GameScript PROPERTIES
    OUTPUT_NAME "GameScript"
    # Editor が Project root から build 成果物を一意に解決できるよう、構成別の固定 directory へ出力する。
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Binaries/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Binaries/$<CONFIG>"
)
