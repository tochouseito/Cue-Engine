cmake_minimum_required(VERSION 4.2.0)

if(NOT EXISTS "${REPORT_FILE}")
    message(FATAL_ERROR "Runtime Host dependency report does not exist: ${REPORT_FILE}")
endif()

file(STRINGS "${REPORT_FILE}" dependencyReportLines)

foreach(
    requiredLine
    IN ITEMS
        "CueRuntimeHost LINK_LIBRARIES: Cue.Foundation;Cue.Platform.Windows;Cue.RHI.D3D12.Windows;Cue.Platform.Windows.TestSupport"
        "Allowed direct dependencies: Cue.Foundation;Cue.Platform.Windows;Cue.RHI.D3D12.Windows"
        "Testing-only direct dependency: Cue.Platform.Windows.TestSupport"
        "Forbidden source dependencies: WindowsSDK;D3D12NativeTypes;Renderer;Editor;ECS;Asset"
)
    set(hasRequiredLine FALSE)

    foreach(reportLine IN LISTS dependencyReportLines)
        if(reportLine STREQUAL requiredLine)
            set(hasRequiredLine TRUE)
            break()
        endif()
    endforeach()

    if(NOT hasRequiredLine)
        message(FATAL_ERROR "Runtime Host dependency report is missing an exact line: ${requiredLine}")
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
        "Windows\\.h|WideCharToMultiByte|MultiByteToWideChar|d3d12\\.h|dxgi[0-9_]*\\.h|ID3D12|IDXGI|D3D12_|DXGI_|DirectX|Renderer|Editor|ECS|Asset"
        forbiddenDependency
        "${sourceContents}"
    )

    if(forbiddenDependency)
        message(FATAL_ERROR "Runtime Host source contains a forbidden dependency: ${runtimeHostSource}")
    endif()
endforeach()

message(STATUS "CueRuntimeHost dependency direction: passed")
