# CueEngine Rebuild の正式なビルド入口を定義する
cmake_minimum_required(VERSION 4.2.0)

project(CueEngine LANGUAGES CXX)

if(CMAKE_CONFIGURATION_TYPES)
    set(
        CMAKE_CONFIGURATION_TYPES
        "Debug;Development;Release"
        CACHE STRING
        "Supported CueEngine build configurations"
        FORCE
    )
endif()

add_executable(CueBuildProbe)

target_sources(
    CueBuildProbe
    PRIVATE
        Source/CueBuildProbe/Main.cpp
)

target_compile_features(CueBuildProbe PRIVATE cxx_std_20)

set_target_properties(
    CueBuildProbe
    PROPERTIES
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

if(MSVC)
    target_compile_options(
        CueBuildProbe
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )

    target_link_options(
        CueBuildProbe
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()
