cmake_minimum_required(VERSION 4.2.0)

project(TestProjectScripts VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(_cue_engine_search_root "${CMAKE_CURRENT_SOURCE_DIR}")
set(CUE_ENGINE_ROOT "")

while(TRUE)
    if(EXISTS "${_cue_engine_search_root}/Engine/Source/Runtime/Core/Native/ScriptAbi.h")
        set(CUE_ENGINE_ROOT "${_cue_engine_search_root}")
        break()
    endif()

    get_filename_component(_cue_engine_parent "${_cue_engine_search_root}" DIRECTORY)
    if(_cue_engine_parent STREQUAL _cue_engine_search_root)
        break()
    endif()

    set(_cue_engine_search_root "${_cue_engine_parent}")
endwhile()

if(CUE_ENGINE_ROOT STREQUAL "" AND DEFINED ENV{CUE_ENGINE_ROOT})
    set(CUE_ENGINE_ROOT "$ENV{CUE_ENGINE_ROOT}")
endif()

if(NOT EXISTS "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core/Native/ScriptAbi.h")
    message(FATAL_ERROR
        "Cue Engine root not found. Place the project under the CueEngine repository "
        "or set CUE_ENGINE_ROOT.")
endif()

unset(_cue_engine_search_root)
unset(_cue_engine_parent)

file(GLOB_RECURSE GAME_SCRIPT_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*Script.cpp"
)

file(GLOB_RECURSE GAME_SCRIPT_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Scripts/*Script.h"
)

set(CUE_SCRIPT_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Intermediate/Generated")
set(CUE_SCRIPT_REGISTRY_CPP "${CUE_SCRIPT_GENERATED_DIR}/ScriptRegistry.gen.cpp")
set(CUE_ENGINE_OUTPUT_DIR "${CUE_ENGINE_ROOT}/generated/outputs/Sdk/Lib/$<CONFIG>")

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

add_library(GameSources OBJECT
    "EngineModule/ScriptRegistry.h"
    ${GAME_SCRIPT_SOURCES}
    "${CUE_SCRIPT_REGISTRY_CPP}"
)

target_include_directories(GameSources PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/EngineModule"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine"
    "${CUE_SCRIPT_GENERATED_DIR}"
)

if(MSVC)
    target_compile_options(GameSources PRIVATE
        /utf-8
        /W4
        /EHsc
        /wd4201
    )
endif()

add_library(GameScript SHARED
    $<TARGET_OBJECTS:GameSources>
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

add_library(Game STATIC
    $<TARGET_OBJECTS:GameSources>
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine/ScriptFramework/GameScriptModule.cpp"
)

target_include_directories(Game PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/EngineModule"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine"
    "${CUE_SCRIPT_GENERATED_DIR}"
)

if(MSVC)
    target_compile_options(Game PRIVATE
        /utf-8
        /W4
        /EHsc
        /wd4201
    )
endif()

set_target_properties(Game PROPERTIES
    OUTPUT_NAME "Game"
)

add_executable(CueApp WIN32
    "${CUE_ENGINE_ROOT}/Engine/Source/App/App/App.cpp"
)

target_include_directories(CueApp PRIVATE
    "${CUE_ENGINE_ROOT}"
    "${CUE_ENGINE_ROOT}/Engine/Source/App/App"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Base"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Math"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/ECS"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/PAL"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/PAL/Win"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/RHI"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/RHI/D3D12"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine"
)

target_compile_definitions(CueApp PRIVATE
    CUE_PROJECT_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}"
    CUE_STATIC_GAME_LINK=1
)

if(MSVC)
    target_compile_options(CueApp PRIVATE
        /utf-8
        /W4
        /EHsc
        /wd4201
    )
endif()

target_link_libraries(CueApp PRIVATE
    Game
    "${CUE_ENGINE_OUTPUT_DIR}/Base.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/Core.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/CueMath.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/ECS.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/PAL.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/RHI.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/Engine.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/win_platform.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/d3d12_backend.lib"
    winmm.lib
    ole32.lib
    shell32.lib
    uuid.lib
    d3d12.lib
    dxgi.lib
    dxcompiler.lib
    dxguid.lib
)
