# 旧 CueEngine ECS を隔離 Checkout から参照する比較専用 Target を定義する
cmake_minimum_required(VERSION 4.2.0)

project(CueLegacyEcsBenchmark LANGUAGES CXX)

if(CMAKE_CONFIGURATION_TYPES)
    set(
        CMAKE_CONFIGURATION_TYPES
        "Debug;Development;Release"
        CACHE STRING
        "Supported legacy ECS benchmark configurations"
        FORCE
    )
endif()

set(CUE_LEGACY_ROOT "" CACHE PATH "Path to an isolated CueEngine release checkout")

if(NOT CUE_LEGACY_ROOT)
    message(FATAL_ERROR "CUE_LEGACY_ROOT must point to an isolated CueEngine release checkout")
endif()

cmake_path(ABSOLUTE_PATH CUE_LEGACY_ROOT NORMALIZE OUTPUT_VARIABLE legacyRoot)

set(legacyEcsRoot "${legacyRoot}/Engine/Source/Runtime/ECS")
set(legacyCoreRoot "${legacyRoot}/Engine/Source/Runtime/Core")
set(legacyMathRoot "${legacyRoot}/Engine/Source/Runtime/Math")

foreach(requiredFile
    "${legacyEcsRoot}/ECSManager.h"
    "${legacyEcsRoot}/ECSManager.cpp"
    "${legacyEcsRoot}/EngineComponentID.cpp"
    "${legacyCoreRoot}/Time/IClock.h"
    "${legacyMathRoot}/TimeUnit.h"
)
    if(NOT EXISTS "${requiredFile}")
        message(FATAL_ERROR "Legacy ECS dependency is missing: ${requiredFile}")
    endif()
endforeach()

find_package(Git QUIET)
set(legacyCommit "unknown")

if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${legacyRoot}" rev-parse HEAD
        OUTPUT_VARIABLE legacyCommit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE legacyCommitResult
    )

    if(NOT legacyCommitResult EQUAL 0)
        set(legacyCommit "unknown")
    endif()
endif()

add_executable(CueLegacyEcsBenchmark)

target_sources(
    CueLegacyEcsBenchmark
    PRIVATE
        Main.cpp
        "${legacyEcsRoot}/ECSManager.cpp"
        "${legacyEcsRoot}/EngineComponentID.cpp"
)

target_include_directories(
    CueLegacyEcsBenchmark
    PRIVATE
        "${legacyEcsRoot}"
        "${legacyCoreRoot}"
        "${legacyMathRoot}"
)

target_compile_features(CueLegacyEcsBenchmark PRIVATE cxx_std_20)

target_compile_definitions(
    CueLegacyEcsBenchmark
    PRIVATE
        CUE_LEGACY_COMMIT="${legacyCommit}"
)

set_target_properties(
    CueLegacyEcsBenchmark
    PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

if(MSVC)
    target_compile_options(
        CueLegacyEcsBenchmark
        PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )

    target_link_options(
        CueLegacyEcsBenchmark
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()
