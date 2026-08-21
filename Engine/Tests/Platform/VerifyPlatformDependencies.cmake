if(NOT EXISTS "${REPORT_FILE}")
    message(FATAL_ERROR "Platform dependency report does not exist: ${REPORT_FILE}")
endif()

file(READ "${REPORT_FILE}" dependencyReport)

foreach(
    requiredLine
    IN ITEMS
        "Cue.Platform LINK_LIBRARIES: Cue.Foundation"
        "Cue.Platform INTERFACE_LINK_LIBRARIES: Cue.Foundation"
        "Cue.Platform.Windows INTERFACE_LINK_LIBRARIES: Cue.Platform"
        "Cue.Platform MANUALLY_ADDED_DEPENDENCIES: <none>"
        "Cue.Platform INTERFACE_SOURCES: <none>"
        "Cue.Platform INTERFACE_LINK_LIBRARIES_DIRECT: <none>"
        "Cue.Platform.Windows MANUALLY_ADDED_DEPENDENCIES: <none>"
        "Cue.Platform.Windows INTERFACE_SOURCES: <none>"
        "Cue.Platform.Windows INTERFACE_LINK_LIBRARIES_DIRECT: <none>"
        "Generated and target object sources: <none>"
        "Allowed Windows PRIVATE links: User32"
        "Forbidden dependencies: Cue.RHI;D3D12;Editor"
        "Windows SDK include boundary: Windows/Private"
)
    string(FIND "${dependencyReport}" "${requiredLine}" linePosition)

    if(linePosition EQUAL -1)
        message(FATAL_ERROR "Platform dependency report is missing: ${requiredLine}")
    endif()
endforeach()

file(
    GLOB_RECURSE
    platformSources
    LIST_DIRECTORIES FALSE
    "${PLATFORM_SOURCE_DIR}/*.cpp"
    "${PLATFORM_SOURCE_DIR}/*.h"
)

set(hasWindowsSdkInclude FALSE)

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

foreach(platformSource IN LISTS platformSources)
    file(READ "${platformSource}" sourceContents)
    file(RELATIVE_PATH relativeSource "${PLATFORM_SOURCE_DIR}" "${platformSource}")
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
        set(isWindowsSdkInclude FALSE)

        foreach(includeRoot IN LISTS windowsPlatformIncludeRoots)
            if(EXISTS "${includeRoot}/${includedHeader}")
                set(isWindowsSdkInclude TRUE)
                break()
            endif()
        endforeach()

        if(isWindowsSdkInclude)
            if(NOT relativeSource MATCHES "^Windows/Private/")
                message(
                    FATAL_ERROR
                    "Windows SDK include escaped the Windows private boundary: ${relativeSource}: ${includedHeader}"
                )
            endif()

            set(hasWindowsSdkInclude TRUE)
        endif()
    endforeach()

    if(relativeSource MATCHES "(^|/)Public/")
        string(
            REGEX MATCH
            "(^|[^A-Za-z0-9_])(HWND|HINSTANCE|LRESULT)([^A-Za-z0-9_]|$)"
            exposedWin32Type
            "${sourceContents}"
        )

        if(exposedWin32Type)
            message(FATAL_ERROR "Public Platform header exposes a Win32 type: ${relativeSource}")
        endif()
    endif()
endforeach()

if(NOT hasWindowsSdkInclude)
    message(FATAL_ERROR "Cue.Platform.Windows does not exercise a private Windows SDK include")
endif()

message(STATUS "Cue.Platform dependency direction: passed")
message(STATUS "Cue.Platform Windows SDK private boundary: passed")
