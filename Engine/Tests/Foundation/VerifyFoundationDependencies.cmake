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

if(NOT DEFINED BUILD_DIR OR NOT IS_DIRECTORY "${BUILD_DIR}")
    message(FATAL_ERROR "Build directory does not exist")
endif()

if(NOT DEFINED TARGET_GRAPH_FILE OR "${TARGET_GRAPH_FILE}" STREQUAL "")
    message(FATAL_ERROR "Target graph output path is not available")
endif()

if(NOT DEFINED DUMPBIN_EXECUTABLE OR NOT EXISTS "${DUMPBIN_EXECUTABLE}")
    message(FATAL_ERROR "dumpbin executable is not available")
endif()

if(NOT DEFINED FOUNDATION_LIBRARY OR NOT EXISTS "${FOUNDATION_LIBRARY}")
    message(FATAL_ERROR "Cue.Foundation library is not available")
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        "--graphviz=${TARGET_GRAPH_FILE}"
        "${BUILD_DIR}"
    RESULT_VARIABLE graphResult
    OUTPUT_VARIABLE graphOutput
    ERROR_VARIABLE graphError
)

if(NOT graphResult EQUAL 0 OR NOT EXISTS "${TARGET_GRAPH_FILE}")
    message(
        FATAL_ERROR
        "CMake target graph generation failed: ${graphOutput}${graphError}"
    )
endif()

file(READ "${TARGET_GRAPH_FILE}" targetGraph)
string(
    REGEX MATCH
    "\"([^\"]+)\"[ \t]*\\[[ \t]*label[ \t]*=[ \t]*\"Cue\\.Foundation\""
    foundationGraphNodeMatch
    "${targetGraph}"
)

if(NOT foundationGraphNodeMatch)
    message(FATAL_ERROR "Cue.Foundation is missing from the generated CMake target graph")
endif()

set(foundationGraphNode "${CMAKE_MATCH_1}")
string(
    REGEX MATCH
    "\"${foundationGraphNode}\"[ \t]*->[^\n]*"
    foundationOutgoingEdge
    "${targetGraph}"
)

if(foundationOutgoingEdge)
    message(
        FATAL_ERROR
        "Cue.Foundation has an outgoing generated target edge: ${foundationOutgoingEdge}"
    )
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
        "TARGET_GRAPH_OUTGOING_EDGES: <none>"
        "Forbidden platform link inputs: Windows SDK, DXGI, D3D12"
        "Cycle review: generated CMake target graph has no outgoing Cue.Foundation edge"
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
set(windowsUcrtIncludeRoot "${windowsSdkIncludeRoot}/ucrt")

foreach(includeRoot IN LISTS windowsPlatformIncludeRoots)
    if(NOT IS_DIRECTORY "${includeRoot}")
        message(FATAL_ERROR "Windows SDK include directory does not exist: ${includeRoot}")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${windowsUcrtIncludeRoot}")
    message(FATAL_ERROR "Windows UCRT include directory does not exist: ${windowsUcrtIncludeRoot}")
endif()

set(
    standardCHeaders
    assert.h
    complex.h
    ctype.h
    errno.h
    fenv.h
    float.h
    inttypes.h
    iso646.h
    limits.h
    locale.h
    math.h
    setjmp.h
    signal.h
    stdalign.h
    stdarg.h
    stdatomic.h
    stdbool.h
    stddef.h
    stdint.h
    stdio.h
    stdlib.h
    stdnoreturn.h
    string.h
    tgmath.h
    threads.h
    time.h
    uchar.h
    wchar.h
    wctype.h
)

foreach(sourceFile IN LISTS foundationSources)
    file(READ "${sourceFile}" sourceContents)
    string(TOLOWER "${sourceContents}" sourceContentsLower)
    string(
        REGEX MATCH
        "(^|\n)[ \t]*#[ \t]*pragma[ \t]+comment[ \t]*\\("
        implicitLinkDirective
        "${sourceContentsLower}"
    )

    if(implicitLinkDirective)
        file(RELATIVE_PATH relativeSource "${REPOSITORY_ROOT}" "${sourceFile}")
        message(
            FATAL_ERROR
            "Foundation source contains an MSVC comment directive that can hide linker inputs: ${relativeSource}: ${implicitLinkDirective}"
        )
    endif()

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

        if(EXISTS "${windowsUcrtIncludeRoot}/${includedHeader}")
            string(TOLOWER "${includedHeader}" includedHeaderLower)
            list(FIND standardCHeaders "${includedHeaderLower}" standardHeaderIndex)

            if(standardHeaderIndex EQUAL -1)
                file(RELATIVE_PATH relativeSource "${REPOSITORY_ROOT}" "${sourceFile}")
                message(
                    FATAL_ERROR
                    "Foundation source directly includes a Windows-specific UCRT header: ${relativeSource}: ${includedHeader}"
                )
            endif()
        endif()
    endforeach()
endforeach()

execute_process(
    COMMAND "${DUMPBIN_EXECUTABLE}" /nologo /directives "${FOUNDATION_LIBRARY}"
    RESULT_VARIABLE dumpbinResult
    OUTPUT_VARIABLE dumpbinOutput
    ERROR_VARIABLE dumpbinError
)

if(NOT dumpbinResult EQUAL 0)
    message(FATAL_ERROR "Cue.Foundation directive inspection failed: ${dumpbinError}")
endif()

set(
    allowedDefaultLibraries
    libcmt
    libcmtd
    libcpmt
    libcpmtd
    msvcrt
    msvcrtd
    msvcprt
    msvcprtd
    oldnames
    ucrt
    ucrtd
    vcruntime
    vcruntimed
    legacy_stdio_definitions
    legacy_stdio_wide_specifiers
)

string(REPLACE "\r\n" "\n" dumpbinOutput "${dumpbinOutput}")
string(REPLACE "\r" "\n" dumpbinOutput "${dumpbinOutput}")
string(REPLACE "\n" ";" dumpbinLines "${dumpbinOutput}")

foreach(dumpbinLine IN LISTS dumpbinLines)
    string(STRIP "${dumpbinLine}" linkerDirective)
    string(TOLOWER "${linkerDirective}" linkerDirectiveLower)

    if(linkerDirectiveLower MATCHES "^-[a-z]")
        string(REGEX REPLACE "^-" "/" linkerDirectiveNormalized "${linkerDirectiveLower}")
    else()
        set(linkerDirectiveNormalized "${linkerDirectiveLower}")
    endif()

    if(linkerDirectiveNormalized MATCHES "^/defaultlib:")
        string(REGEX REPLACE "^/defaultlib:[\"]?" "" defaultLibrary "${linkerDirectiveNormalized}")
        string(REGEX REPLACE "[\"]?$" "" defaultLibrary "${defaultLibrary}")
        string(REGEX REPLACE "\\.lib$" "" defaultLibrary "${defaultLibrary}")
        list(FIND allowedDefaultLibraries "${defaultLibrary}" allowedLibraryIndex)

        if(allowedLibraryIndex EQUAL -1)
            message(
                FATAL_ERROR
                "Cue.Foundation object contains a non-runtime default library directive: ${linkerDirective}"
            )
        endif()
    elseif(
        linkerDirectiveNormalized MATCHES "^/"
        AND NOT linkerDirectiveNormalized MATCHES "^/failifmismatch:|^/alternatename:"
    )
        message(
            FATAL_ERROR
            "Cue.Foundation object contains an unapproved linker directive: ${linkerDirective}"
        )
    endif()
endforeach()

message(STATUS "Foundation dependency report: ${REPORT_FILE}")
message(STATUS "Cue.Foundation generated target graph outgoing edges: <none>")
message(STATUS "Foundation Windows SDK and UCRT platform header resolution: passed")
message(STATUS "Foundation implicit MSVC linker directive scan: passed")
message(STATUS "Cue.Foundation object linker directive inspection: passed")
