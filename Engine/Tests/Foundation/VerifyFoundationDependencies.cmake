cmake_minimum_required(VERSION 4.2.0)

if(NOT DEFINED REPORT_FILE OR NOT EXISTS "${REPORT_FILE}")
    message(FATAL_ERROR "Foundation dependency report was not generated")
endif()

if(NOT DEFINED FOUNDATION_SOURCE_DIR OR NOT IS_DIRECTORY "${FOUNDATION_SOURCE_DIR}")
    message(FATAL_ERROR "Foundation source directory does not exist")
endif()

if(NOT DEFINED REPOSITORY_ROOT OR NOT IS_DIRECTORY "${REPOSITORY_ROOT}")
    message(FATAL_ERROR "Repository root does not exist")
endif()

file(READ "${REPORT_FILE}" dependencyReport)

foreach(
    requiredLine
    IN ITEMS
        "LINK_LIBRARIES: <none>"
        "INTERFACE_LINK_LIBRARIES: <none>"
        "MANUALLY_ADDED_DEPENDENCIES: <none>"
        "Forbidden platform link inputs: Windows SDK, DXGI, D3D12"
        "Cycle review: no outgoing target link or manual dependency edge"
)
    string(FIND "${dependencyReport}" "${requiredLine}" linePosition)

    if(linePosition EQUAL -1)
        message(FATAL_ERROR "Dependency report is missing: ${requiredLine}")
    endif()
endforeach()

file(
    GLOB_RECURSE
    foundationSources
    LIST_DIRECTORIES FALSE
    "${FOUNDATION_SOURCE_DIR}/*.cc"
    "${FOUNDATION_SOURCE_DIR}/*.cpp"
    "${FOUNDATION_SOURCE_DIR}/*.cxx"
    "${FOUNDATION_SOURCE_DIR}/*.h"
    "${FOUNDATION_SOURCE_DIR}/*.hpp"
    "${FOUNDATION_SOURCE_DIR}/*.inl"
)

set(
    forbiddenIncludePattern
    "#[ \t]*include[ \t]*[<\"](windows\.h|windowsx\.h|winbase\.h|winerror\.h|minwindef\.h|unknwn\.h|wrl\.h|dxgi[^>\"]*|d3d[^>\"]*|directx/[^>\"]*)[>\"]"
)

foreach(sourceFile IN LISTS foundationSources)
    file(READ "${sourceFile}" sourceContents)
    string(TOLOWER "${sourceContents}" sourceContentsLower)
    string(REGEX MATCH "${forbiddenIncludePattern}" forbiddenInclude "${sourceContentsLower}")

    if(forbiddenInclude)
        file(RELATIVE_PATH relativeSource "${REPOSITORY_ROOT}" "${sourceFile}")
        message(
            FATAL_ERROR
            "Foundation source includes a forbidden platform header: ${relativeSource}: ${forbiddenInclude}"
        )
    endif()
endforeach()

message(STATUS "Foundation dependency report: ${REPORT_FILE}")
message(STATUS "Cue.Foundation target dependency inputs: <none>")
message(STATUS "Foundation platform header scan: passed")
