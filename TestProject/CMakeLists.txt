cmake_minimum_required(VERSION 4.2.0)

project(TestProjectScripts VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CUE_ENGINE_ROOT "C:/Users/sinse/source/repos/CueEngine")

if(NOT EXISTS "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core/Native/ScriptAbi.h")
    message(FATAL_ERROR "Cue Engine root not found: ${CUE_ENGINE_ROOT}")
endif()

file(GLOB_RECURSE GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*Script.cpp"
)

file(GLOB_RECURSE GAME_SCRIPT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*Script.h"
)

set(CUE_SCRIPT_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Intermediate/Generated")
set(CUE_SCRIPT_REGISTRY_CPP "${CUE_SCRIPT_GENERATED_DIR}/ScriptRegistry.gen.cpp")

add_custom_command(
    OUTPUT "${CUE_SCRIPT_REGISTRY_CPP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CUE_SCRIPT_GENERATED_DIR}"
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DASSET_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/Assets
        -DSCRIPT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts
        -DOUTPUT=${CUE_SCRIPT_REGISTRY_CPP}
        -P "${CUE_ENGINE_ROOT}/Tools/CMake/GenerateScriptRegistry.cmake"
    DEPENDS
        ${GAME_SCRIPT_HEADERS}
        "${CMAKE_CURRENT_SOURCE_DIR}/EngineModule/ScriptRegistry.h"
        "${CUE_ENGINE_ROOT}/Tools/CMake/GenerateScriptRegistry.cmake"
    VERBATIM
)

add_library(GameScript SHARED
    "EngineModule/ScriptRegistry.h"
    ${GAME_SCRIPT_SOURCES}
    "${CUE_SCRIPT_REGISTRY_CPP}"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine/ScriptFramework/GameScriptModule.cpp"
)

target_include_directories(GameScript PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/EngineModule"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine"
    "${CUE_SCRIPT_GENERATED_DIR}"
)

target_compile_definitions(GameScript PRIVATE
    CUE_SCRIPT_DLL_EXPORTS=1
)

if(MSVC)
    target_compile_options(GameScript PRIVATE
        /utf-8
        /W4
        /EHsc
        /wd4201
    )
endif()

set_target_properties(GameScript PROPERTIES
    OUTPUT_NAME "GameScript"
)
