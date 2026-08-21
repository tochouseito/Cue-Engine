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
        "INTERFACE_LINK_OPTIONS: <none>"
        "INTERFACE_LINK_LIBRARIES_DIRECT: <none>"
        "INTERFACE_SOURCES: <none>"
        "TARGET_OBJECT_SOURCES: <none>"
        "Forbidden platform link inputs: Windows SDK, DXGI, D3D12"
        "Cycle review: no outgoing link, interface source, target object, or manual dependency edge"
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

if(NOT WIN32)
    message(FATAL_ERROR "Windows SDK dependency verification requires a Windows host")
endif()

if(NOT DEFINED WINDOWS_SDK_VERSION OR "${WINDOWS_SDK_VERSION}" STREQUAL "")
    message(FATAL_ERROR "Selected Windows SDK version is not available")
endif()

cmake_host_system_information(
    RESULT windowsSdkRoot
    QUERY WINDOWS_REGISTRY "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
    VALUE KitsRoot10
    VIEW HOST
    ERROR_VARIABLE windowsSdkRegistryError
)

if(NOT "${windowsSdkRegistryError}" STREQUAL "" OR NOT IS_DIRECTORY "${windowsSdkRoot}")
    message(FATAL_ERROR "Windows SDK root could not be resolved: ${windowsSdkRegistryError}")
endif()

set(windowsSdkIncludeRoot "${windowsSdkRoot}/Include/${WINDOWS_SDK_VERSION}")
set(
    windowsPlatformIncludeRoots
    "${windowsSdkIncludeRoot}/um"
    "${windowsSdkIncludeRoot}/shared"
    "${windowsSdkIncludeRoot}/winrt"
    "${windowsSdkIncludeRoot}/cppwinrt"
)

foreach(includeRoot IN LISTS windowsPlatformIncludeRoots)
    if(NOT IS_DIRECTORY "${includeRoot}")
        message(FATAL_ERROR "Windows SDK include directory does not exist: ${includeRoot}")
    endif()
endforeach()

foreach(sourceFile IN LISTS foundationSources)
    file(READ "${sourceFile}" sourceContents)
    string(
        REGEX MATCHALL
        "#[ \t]*include[ \t]*[<\"][^>\"]+[>\"]"
        includeDirectives
        "${sourceContents}"
    )

    foreach(includeDirective IN LISTS includeDirectives)
        string(
            REGEX REPLACE
            "^#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]$"
            "\\1"
            includedHeader
            "${includeDirective}"
        )

        foreach(includeRoot IN LISTS windowsPlatformIncludeRoots)
            if(EXISTS "${includeRoot}/${includedHeader}")
                file(RELATIVE_PATH relativeSource "${REPOSITORY_ROOT}" "${sourceFile}")
                message(
                    FATAL_ERROR
                    "Foundation source directly includes a Windows SDK platform header: ${relativeSource}: ${includedHeader}"
                )
            endif()
        endforeach()
    endforeach()
endforeach()

message(STATUS "Foundation dependency report: ${REPORT_FILE}")
message(STATUS "Cue.Foundation outgoing target dependency inputs: <none>")
message(STATUS "Foundation Windows SDK platform header resolution: passed")
