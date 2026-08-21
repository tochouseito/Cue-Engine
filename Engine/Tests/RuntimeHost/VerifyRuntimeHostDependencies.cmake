cmake_minimum_required(VERSION 4.2.0)

if(NOT EXISTS "${REPORT_FILE}")
    message(FATAL_ERROR "Runtime Host dependency report does not exist: ${REPORT_FILE}")
endif()

file(READ "${REPORT_FILE}" dependencyReport)

foreach(
    requiredLine
    IN ITEMS
        "CueRuntimeHost LINK_LIBRARIES: Cue.Foundation;Cue.Platform.Windows"
        "Allowed direct dependencies: Cue.Foundation;Cue.Platform.Windows"
        "Forbidden dependencies: Cue.RHI;D3D12;Renderer;Editor;ECS;Asset"
)
    string(FIND "${dependencyReport}" "${requiredLine}" linePosition)

    if(linePosition EQUAL -1)
        message(FATAL_ERROR "Runtime Host dependency report is missing: ${requiredLine}")
    endif()
endforeach()

file(
    GLOB_RECURSE
    runtimeHostSources
    LIST_DIRECTORIES FALSE
    "${RUNTIME_HOST_SOURCE_DIR}/*.c"
    "${RUNTIME_HOST_SOURCE_DIR}/*.cc"
    "${RUNTIME_HOST_SOURCE_DIR}/*.cpp"
    "${RUNTIME_HOST_SOURCE_DIR}/*.cxx"
    "${RUNTIME_HOST_SOURCE_DIR}/*.h"
    "${RUNTIME_HOST_SOURCE_DIR}/*.hh"
    "${RUNTIME_HOST_SOURCE_DIR}/*.hpp"
    "${RUNTIME_HOST_SOURCE_DIR}/*.hxx"
)

foreach(runtimeHostSource IN LISTS runtimeHostSources)
    file(READ "${runtimeHostSource}" sourceContents)
    string(
        REGEX MATCH
        "Cue/RHI|d3d12\\.h|DirectX|Renderer|Editor|ECS|Asset"
        forbiddenDependency
        "${sourceContents}"
    )

    if(forbiddenDependency)
        message(FATAL_ERROR "Runtime Host source contains a forbidden dependency: ${runtimeHostSource}")
    endif()
endforeach()

message(STATUS "CueRuntimeHost dependency direction: passed")
