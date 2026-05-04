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
set(CUE_ENGINE_VCPKG_TARGET_TRIPLET "x64-windows-static-md")
set(CUE_ENGINE_VCPKG_HOST_TRIPLET "x64-windows")
set(CUE_ENGINE_VCPKG_TARGET_ROOT
    "${CUE_ENGINE_ROOT}/out/build/win-x64/vcpkg_installed/${CUE_ENGINE_VCPKG_TARGET_TRIPLET}")
set(CUE_ENGINE_VCPKG_HOST_ROOT
    "${CUE_ENGINE_ROOT}/out/build/win-x64/vcpkg_installed/${CUE_ENGINE_VCPKG_HOST_TRIPLET}")

set(CUE_GAME_RELEASE_EXECUTABLE_NAME "Game")
set(CUE_GAME_RELEASE_WINDOW_TITLE "Cue App")
set(CUE_GAME_RELEASE_ICON_PATH "")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cueproject.json")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cueproject.json" CUE_PROJECT_JSON)

    string(JSON CUE_PROJECT_NAME ERROR_VARIABLE CUE_PROJECT_JSON_ERROR
        GET "${CUE_PROJECT_JSON}" name)
    if(CUE_PROJECT_JSON_ERROR STREQUAL "NOTFOUND" AND NOT CUE_PROJECT_NAME STREQUAL "")
        set(CUE_GAME_RELEASE_WINDOW_TITLE "${CUE_PROJECT_NAME}")
    endif()

    string(JSON CUE_PROJECT_EXE_NAME ERROR_VARIABLE CUE_PROJECT_JSON_ERROR
        GET "${CUE_PROJECT_JSON}" gameReleaseExecutableName)
    if(CUE_PROJECT_JSON_ERROR STREQUAL "NOTFOUND" AND NOT CUE_PROJECT_EXE_NAME STREQUAL "")
        set(CUE_GAME_RELEASE_EXECUTABLE_NAME "${CUE_PROJECT_EXE_NAME}")
    endif()

    string(JSON CUE_PROJECT_WINDOW_TITLE ERROR_VARIABLE CUE_PROJECT_JSON_ERROR
        GET "${CUE_PROJECT_JSON}" gameReleaseWindowTitle)
    if(CUE_PROJECT_JSON_ERROR STREQUAL "NOTFOUND" AND NOT CUE_PROJECT_WINDOW_TITLE STREQUAL "")
        set(CUE_GAME_RELEASE_WINDOW_TITLE "${CUE_PROJECT_WINDOW_TITLE}")
    endif()

    string(JSON CUE_PROJECT_ICON_PATH ERROR_VARIABLE CUE_PROJECT_JSON_ERROR
        GET "${CUE_PROJECT_JSON}" gameReleaseIconPath)
    if(CUE_PROJECT_JSON_ERROR STREQUAL "NOTFOUND" AND NOT CUE_PROJECT_ICON_PATH STREQUAL "")
        set(CUE_GAME_RELEASE_ICON_PATH "${CUE_PROJECT_ICON_PATH}")
    endif()
endif()

if(CUE_GAME_RELEASE_EXECUTABLE_NAME MATCHES "\\.[eE][xX][eE]$")
    get_filename_component(CUE_GAME_RELEASE_EXECUTABLE_NAME
        "${CUE_GAME_RELEASE_EXECUTABLE_NAME}" NAME_WE)
endif()

if(CUE_GAME_RELEASE_WINDOW_TITLE STREQUAL "")
    set(CUE_GAME_RELEASE_WINDOW_TITLE "Cue App")
endif()

set(CUE_GAME_RELEASE_RESOURCE_SOURCES "")
if(NOT CUE_GAME_RELEASE_ICON_PATH STREQUAL "")
    if(NOT IS_ABSOLUTE "${CUE_GAME_RELEASE_ICON_PATH}")
        set(CUE_GAME_RELEASE_ICON_PATH
            "${CMAKE_CURRENT_SOURCE_DIR}/${CUE_GAME_RELEASE_ICON_PATH}")
    endif()

    if(EXISTS "${CUE_GAME_RELEASE_ICON_PATH}")
        get_filename_component(
            CUE_GAME_RELEASE_ICON_EXTENSION
            "${CUE_GAME_RELEASE_ICON_PATH}"
            EXT)
        string(TOLOWER
            "${CUE_GAME_RELEASE_ICON_EXTENSION}"
            CUE_GAME_RELEASE_ICON_EXTENSION)

        if(CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL ".ico")
            set(CUE_GAME_RELEASE_ICON_RESOURCE_PATH
                "${CUE_GAME_RELEASE_ICON_PATH}")
        elseif(
            CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL ".png" OR
            CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL ".jpg" OR
            CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL ".jpeg" OR
            CUE_GAME_RELEASE_ICON_EXTENSION STREQUAL ".bmp")
            find_program(CUE_PWSH_EXECUTABLE NAMES pwsh powershell)
            if(CUE_PWSH_EXECUTABLE STREQUAL "CUE_PWSH_EXECUTABLE-NOTFOUND")
                message(FATAL_ERROR
                    "PowerShell was not found. It is required to generate an icon from an image.")
            endif()

            set(CUE_GAME_RELEASE_ICON_RESOURCE_PATH
                "${CMAKE_CURRENT_BINARY_DIR}/CueAppIcon.ico")
            execute_process(
                COMMAND
                    "${CUE_PWSH_EXECUTABLE}"
                    -NoProfile
                    -ExecutionPolicy
                    Bypass
                    -File
                    "${CUE_ENGINE_ROOT}/Tools/PowerShell/GenerateIco.ps1"
                    -InputPath
                    "${CUE_GAME_RELEASE_ICON_PATH}"
                    -OutputPath
                    "${CUE_GAME_RELEASE_ICON_RESOURCE_PATH}"
                RESULT_VARIABLE CUE_GAME_RELEASE_ICON_RESULT
            )
            if(NOT CUE_GAME_RELEASE_ICON_RESULT EQUAL 0)
                message(FATAL_ERROR
                    "Failed to generate an icon from ${CUE_GAME_RELEASE_ICON_PATH}.")
            endif()
        else()
            message(FATAL_ERROR
                "gameReleaseIconPath supports .ico, .png, .jpg, .jpeg and .bmp.")
        endif()

        file(TO_CMAKE_PATH
            "${CUE_GAME_RELEASE_ICON_RESOURCE_PATH}"
            CUE_GAME_RELEASE_ICON_RESOURCE_PATH)
        set(CUE_GAME_RELEASE_RC_PATH
            "${CMAKE_CURRENT_BINARY_DIR}/CueAppResources.rc")
        file(WRITE "${CUE_GAME_RELEASE_RC_PATH}"
            "#define IDI_CUE_APP 101\n\nIDI_CUE_APP ICON \"${CUE_GAME_RELEASE_ICON_RESOURCE_PATH}\"\n")
        list(APPEND CUE_GAME_RELEASE_RESOURCE_SOURCES
            "${CUE_GAME_RELEASE_RC_PATH}")
    endif()
endif()

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
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Math"
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
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Math"
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
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Math"
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
    ${CUE_GAME_RELEASE_RESOURCE_SOURCES}
)

target_include_directories(CueApp PRIVATE
    "${CUE_ENGINE_ROOT}"
    "${CUE_ENGINE_ROOT}/Engine/Source/App/App"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Audio"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Audio/XAudio2"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Base"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Core"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Math"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/ECS"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/PAL"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/PAL/Win"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Physics"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Physics/Jolt"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/RHI"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/RHI/D3D12"
    "${CUE_ENGINE_ROOT}/Engine/Source/Runtime/Engine"
)

target_compile_definitions(CueApp PRIVATE
    CUE_PROJECT_ROOT_PATH="${CMAKE_CURRENT_SOURCE_DIR}"
    CUE_STATIC_GAME_LINK=1
    CUE_APP_WINDOW_TITLE="${CUE_GAME_RELEASE_WINDOW_TITLE}"
)

set_target_properties(CueApp PROPERTIES
    OUTPUT_NAME "${CUE_GAME_RELEASE_EXECUTABLE_NAME}"
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
    "${CUE_ENGINE_OUTPUT_DIR}/Physics.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/RHI.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/Engine.lib"
    "$<IF:$<CONFIG:Debug>,${CUE_ENGINE_VCPKG_TARGET_ROOT}/debug/lib/Detour-d.lib,${CUE_ENGINE_VCPKG_TARGET_ROOT}/lib/Detour.lib>"
    "$<IF:$<CONFIG:Debug>,${CUE_ENGINE_VCPKG_TARGET_ROOT}/debug/lib/Recast-d.lib,${CUE_ENGINE_VCPKG_TARGET_ROOT}/lib/Recast.lib>"
    "${CUE_ENGINE_OUTPUT_DIR}/win_platform.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/jolt_physics_backend.lib"
    "$<IF:$<CONFIG:Debug>,${CUE_ENGINE_VCPKG_TARGET_ROOT}/debug/lib/Jolt.lib,${CUE_ENGINE_VCPKG_TARGET_ROOT}/lib/Jolt.lib>"
    "${CUE_ENGINE_OUTPUT_DIR}/d3d12_backend.lib"
    "${CUE_ENGINE_OUTPUT_DIR}/xaudio2_backend.lib"
    winmm.lib
    ole32.lib
    shell32.lib
    uuid.lib
    d3d12.lib
    dinput8.lib
    dxgi.lib
    dxcompiler.lib
    dxguid.lib
)

add_custom_command(TARGET CueApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CUE_ENGINE_ROOT}/generated/outputs/App/$<CONFIG>/EngineResources"
        "$<TARGET_FILE_DIR:CueApp>/EngineResources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${CUE_ENGINE_VCPKG_HOST_ROOT}/debug/bin/dxcompiler.dll,${CUE_ENGINE_VCPKG_HOST_ROOT}/bin/dxcompiler.dll>"
        "$<TARGET_FILE_DIR:CueApp>/dxcompiler.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,${CUE_ENGINE_VCPKG_HOST_ROOT}/debug/bin/dxil.dll,${CUE_ENGINE_VCPKG_HOST_ROOT}/bin/dxil.dll>"
        "$<TARGET_FILE_DIR:CueApp>/dxil.dll"
    VERBATIM
)
