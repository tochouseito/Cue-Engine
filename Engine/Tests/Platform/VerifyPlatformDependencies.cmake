if(NOT EXISTS "${REPORT_FILE}")
    message(FATAL_ERROR "Platform dependency report does not exist: ${REPORT_FILE}")
endif()

file(READ "${REPORT_FILE}" dependencyReport)

foreach(
    requiredLine
    IN ITEMS
        "Cue.Platform LINK_LIBRARIES: Cue.Foundation"
        "Cue.Platform INTERFACE_LINK_LIBRARIES: Cue.Foundation"
        "Cue.Platform.Windows LINK_LIBRARIES: Cue.Platform"
        "Cue.Platform.Windows INTERFACE_LINK_LIBRARIES: Cue.Platform"
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

foreach(platformSource IN LISTS platformSources)
    file(READ "${platformSource}" sourceContents)
    file(RELATIVE_PATH relativeSource "${PLATFORM_SOURCE_DIR}" "${platformSource}")
    string(
        REGEX MATCH
        "#[ \t]*include[ \t]*[<\"]Windows\\.h[>\"]"
        windowsSdkInclude
        "${sourceContents}"
    )

    if(windowsSdkInclude)
        if(NOT relativeSource MATCHES "^Windows/Private/")
            message(
                FATAL_ERROR
                "Windows SDK include escaped the Windows private boundary: ${relativeSource}"
            )
        endif()

        set(hasWindowsSdkInclude TRUE)
    endif()

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
