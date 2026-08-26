set(cueD3d12MinimumWindowsSdkVersion "10.0.26100.0")

if(NOT CMAKE_GENERATOR MATCHES "^Visual Studio ")
    message(FATAL_ERROR "Cue.RHI.D3D12 requires a Visual Studio generator")
endif()

if("${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}" STREQUAL "")
    message(FATAL_ERROR "Selected Windows SDK version is not available")
endif()

if(
    "${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
    VERSION_LESS
    "${cueD3d12MinimumWindowsSdkVersion}"
)
    message(
        FATAL_ERROR
        "Cue.RHI.D3D12 requires Windows SDK ${cueD3d12MinimumWindowsSdkVersion} or later; selected ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}"
    )
endif()

add_library(Cue.RHI.D3D12 STATIC)

set_property(
    TARGET Cue.RHI.D3D12
    PROPERTY CUE_MINIMUM_WINDOWS_SDK_VERSION "${cueD3d12MinimumWindowsSdkVersion}"
)

target_sources(
    Cue.RHI.D3D12
    PRIVATE
        Private/D3d12AdapterSelection.cpp
        Private/D3d12AdapterSelection.h
        Private/D3d12Backend.cpp
        Private/D3d12DeviceCreation.cpp
        Private/D3d12DeviceCreation.h
        Private/D3d12Diagnostics.cpp
        Private/D3d12Diagnostics.h
        Private/D3d12Error.cpp
        Private/D3d12Error.h
        Private/D3d12FrameCommandState.cpp
        Private/D3d12FrameCommandState.h
        Private/D3d12QueueState.cpp
        Private/D3d12QueueState.h
        Private/D3d12RtvHeap.cpp
        Private/D3d12RtvHeap.h
        Private/D3d12SwapChainState.cpp
        Private/D3d12SwapChainState.h
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS Public
        FILES
            Public/Cue/RHI/D3D12/D3d12Backend.h
)

target_link_libraries(
    Cue.RHI.D3D12
    PUBLIC
        Cue.RHI
    PRIVATE
        D3D12
        DXGI
        DXGUID
)
target_compile_features(Cue.RHI.D3D12 PUBLIC cxx_std_20)

target_compile_definitions(
    Cue.RHI.D3D12
    PRIVATE
        $<$<CONFIG:Debug>:CUE_D3D12_DIAGNOSTICS_ALLOWED=1>
        $<$<CONFIG:Development>:CUE_D3D12_DIAGNOSTICS_ALLOWED=1>
        $<$<CONFIG:Release>:CUE_D3D12_DIAGNOSTICS_ALLOWED=0>
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)

target_include_directories(
    Cue.RHI.D3D12
    PUBLIC
        Public
    PRIVATE
        Private
)

if(BUILD_TESTING)
    target_sources(
        Cue.RHI.D3D12
        PRIVATE
            Private/TestSupport/D3d12AdapterSelectionProbe.cpp
            Private/TestSupport/D3d12BackendProbe.cpp
            Private/TestSupport/D3d12DiagnosticsProbe.cpp
            Private/TestSupport/D3d12FrameCommandProbe.cpp
            Private/TestSupport/D3d12ProbeUtilities.cpp
            Private/TestSupport/D3d12ProbeUtilities.h
            Private/TestSupport/D3d12QueueStateProbe.cpp
            Private/TestSupport/D3d12RtvHeapProbe.cpp
            Private/TestSupport/D3d12SwapChainProbe.cpp
        PUBLIC
            FILE_SET HEADERS
            FILES
                Public/Cue/RHI/D3D12/TestSupport/D3d12AdapterSelectionProbe.h
                Public/Cue/RHI/D3D12/TestSupport/D3d12BackendProbe.h
                Public/Cue/RHI/D3D12/TestSupport/D3d12DiagnosticsProbe.h
                Public/Cue/RHI/D3D12/TestSupport/D3d12FrameCommandProbe.h
                Public/Cue/RHI/D3D12/TestSupport/D3d12QueueStateProbe.h
                Public/Cue/RHI/D3D12/TestSupport/D3d12RtvHeapProbe.h
                Public/Cue/RHI/D3D12/TestSupport/D3d12SwapChainProbe.h
    )
endif()

set_target_properties(
    Cue.RHI.D3D12
    PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>"
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
        Cue.RHI.D3D12
        PRIVATE
            /W4
            /WX
            /permissive-
            /Zc:__cplusplus
            /utf-8
            $<$<CONFIG:Development>:/O2>
            $<$<CONFIG:Development>:/Zi>
    )

    target_link_options(
        Cue.RHI.D3D12
        PRIVATE
            $<$<CONFIG:Development>:/DEBUG>
    )
endif()

add_subdirectory(Windows)
