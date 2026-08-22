function(cue_get_rhi_target_property targetName propertyName outputVariable)
    get_target_property(propertyValue ${targetName} ${propertyName})

    if(propertyValue MATCHES "-NOTFOUND$")
        set(propertyValue "<none>")
    endif()

    set(${outputVariable} "${propertyValue}" PARENT_SCOPE)
endfunction()

function(cue_collect_rhi_target_files targetName outputVariable)
    get_target_property(targetSourceDirectory ${targetName} SOURCE_DIR)
    get_target_property(targetSources ${targetName} SOURCES)
    get_target_property(targetHeaderSets ${targetName} HEADER_SETS)
    get_target_property(targetInterfaceHeaderSets ${targetName} INTERFACE_HEADER_SETS)
    set(targetFiles ${targetSources})

    foreach(headerSetProperty IN ITEMS targetHeaderSets targetInterfaceHeaderSets)
        if(${headerSetProperty} MATCHES "-NOTFOUND$")
            set(${headerSetProperty} "")
        endif()
    endforeach()

    set(allHeaderSets ${targetHeaderSets} ${targetInterfaceHeaderSets})
    list(REMOVE_DUPLICATES allHeaderSets)

    foreach(headerSet IN LISTS allHeaderSets)
        if(headerSet STREQUAL "HEADERS")
            set(headerSetProperty HEADER_SET)
        else()
            set(headerSetProperty "HEADER_SET_${headerSet}")
        endif()

        get_target_property(headerFiles ${targetName} "${headerSetProperty}")

        if(NOT headerFiles MATCHES "-NOTFOUND$")
            list(APPEND targetFiles ${headerFiles})
        endif()
    endforeach()

    set(absoluteTargetFiles "")

    foreach(targetFile IN LISTS targetFiles)
        if(targetFile MATCHES "\\$<")
            message(FATAL_ERROR "${targetName} contains an uninspectable source: ${targetFile}")
        endif()

        get_filename_component(
            absoluteTargetFile
            "${targetFile}"
            ABSOLUTE
            BASE_DIR "${targetSourceDirectory}"
        )
        list(APPEND absoluteTargetFiles "${absoluteTargetFile}")
    endforeach()

    list(REMOVE_DUPLICATES absoluteTargetFiles)
    set(${outputVariable} "${absoluteTargetFiles}" PARENT_SCOPE)
endfunction()

function(cue_verify_rhi_target_shape targetName expectedPublicDirectory)
    foreach(
        forbiddenProperty
        IN ITEMS
            PRECOMPILE_HEADERS
            INTERFACE_PRECOMPILE_HEADERS
            PRECOMPILE_HEADERS_REUSE_FROM
            CXX_MODULE_SETS
            INTERFACE_CXX_MODULE_SETS
    )
        get_target_property(propertyValue ${targetName} ${forbiddenProperty})

        if(NOT propertyValue MATCHES "-NOTFOUND$" AND NOT "${propertyValue}" STREQUAL "")
            message(FATAL_ERROR "${targetName} must not use ${forbiddenProperty}: ${propertyValue}")
        endif()
    endforeach()

    get_target_property(interfaceIncludeDirectories ${targetName} INTERFACE_INCLUDE_DIRECTORIES)

    foreach(interfaceIncludeDirectory IN LISTS interfaceIncludeDirectories)
        string(
            REGEX REPLACE
            "^\\$<BUILD_INTERFACE:(.*)>$"
            "\\1"
            resolvedIncludeDirectory
            "${interfaceIncludeDirectory}"
        )

        if(resolvedIncludeDirectory MATCHES "\\$<")
            message(
                FATAL_ERROR
                "${targetName} contains an uninspectable interface include: ${interfaceIncludeDirectory}"
            )
        endif()

        get_filename_component(resolvedIncludeDirectory "${resolvedIncludeDirectory}" ABSOLUTE)
        get_filename_component(expectedPublicDirectory "${expectedPublicDirectory}" ABSOLUTE)

        if(NOT resolvedIncludeDirectory STREQUAL expectedPublicDirectory)
            message(
                FATAL_ERROR
                "${targetName} exposes an unexpected include directory: ${resolvedIncludeDirectory}"
            )
        endif()
    endforeach()
endfunction()

add_executable(
    CueRhiPublicHeaderTest
    PublicHeaderCompile.cpp
    GraphicsBackendHeaderCompile.cpp
)

target_link_libraries(CueRhiPublicHeaderTest PRIVATE Cue.RHI)
target_compile_features(CueRhiPublicHeaderTest PRIVATE cxx_std_20)

set(cueRhiTestTargets CueRhiPublicHeaderTest)
set(cueRhiTests Cue.RHI.PublicHeaders)

if(WIN32)
    add_executable(
        CueRhiD3d12PublicHeaderTest
        PublicHeaderCompile.cpp
        D3d12BackendHeaderCompile.cpp
    )
    add_executable(CueRhiD3d12AdapterSelectionProcess D3d12AdapterSelectionProcess.cpp)
    add_executable(CueRhiD3d12BackendProcess D3d12BackendProcess.cpp)
    add_executable(CueRhiD3d12DiagnosticsProcess D3d12DiagnosticsProcess.cpp)
    add_executable(CueRhiD3d12FrameCommandProcess D3d12FrameCommandProcess.cpp)
    add_executable(CueRhiD3d12QueueStateProcess D3d12QueueStateProcess.cpp)

    target_link_libraries(CueRhiD3d12PublicHeaderTest PRIVATE Cue.RHI.D3D12)
    target_compile_features(CueRhiD3d12PublicHeaderTest PRIVATE cxx_std_20)
    target_link_libraries(CueRhiD3d12AdapterSelectionProcess PRIVATE Cue.RHI.D3D12)
    target_compile_features(CueRhiD3d12AdapterSelectionProcess PRIVATE cxx_std_20)
    target_link_libraries(CueRhiD3d12BackendProcess PRIVATE Cue.RHI.D3D12)
    target_compile_features(CueRhiD3d12BackendProcess PRIVATE cxx_std_20)
    target_link_libraries(CueRhiD3d12DiagnosticsProcess PRIVATE Cue.RHI.D3D12)
    target_compile_features(CueRhiD3d12DiagnosticsProcess PRIVATE cxx_std_20)
    target_link_libraries(CueRhiD3d12FrameCommandProcess PRIVATE Cue.RHI.D3D12)
    target_compile_features(CueRhiD3d12FrameCommandProcess PRIVATE cxx_std_20)
    target_link_libraries(CueRhiD3d12QueueStateProcess PRIVATE Cue.RHI.D3D12)
    target_compile_features(CueRhiD3d12QueueStateProcess PRIVATE cxx_std_20)
    list(
        APPEND
        cueRhiTestTargets
        CueRhiD3d12PublicHeaderTest
        CueRhiD3d12AdapterSelectionProcess
        CueRhiD3d12BackendProcess
        CueRhiD3d12DiagnosticsProcess
        CueRhiD3d12FrameCommandProcess
        CueRhiD3d12QueueStateProcess
    )
    list(
        APPEND
        cueRhiTests
        Cue.RHI.D3D12.PublicHeaders
        Cue.RHI.D3D12.AdapterSelection.Hardware
        Cue.RHI.D3D12.AdapterSelection.Warp
        Cue.RHI.D3D12.AdapterSelection.Skip
        Cue.RHI.D3D12.AdapterSelection.Mapping
        Cue.RHI.D3D12.AdapterSelection.FactoryFallback
        Cue.RHI.D3D12.AdapterSelection.FactoryFallbackLogFailure
        Cue.RHI.D3D12.AdapterSelection.LogFailure
        Cue.RHI.D3D12.Backend.Warp
        Cue.RHI.D3D12.Backend.DeviceFailure
        Cue.RHI.D3D12.Backend.ThreadDestruction
        Cue.RHI.D3D12.Backend.InvalidWaitTimeout
        Cue.RHI.D3D12.Diagnostics.Disabled
        Cue.RHI.D3D12.Diagnostics.Configured
        Cue.RHI.D3D12.FrameCommand.Smoke300
        Cue.RHI.D3D12.FrameCommand.InfoQueue300
        Cue.RHI.D3D12.FrameCommand.InvalidIndex
        Cue.RHI.D3D12.FrameCommand.WaitRecoveryMatrix
        Cue.RHI.D3D12.FrameCommand.BeginFrameTerminalOutcomeMatrix
        Cue.RHI.D3D12.FrameCommand.FrameResetFailedTerminalMatrix
        Cue.RHI.D3D12.FrameCommand.RecordingCloseFailedTerminalMatrix
        Cue.RHI.D3D12.FrameCommand.FenceExhaustionMatrix
        Cue.RHI.D3D12.FrameCommand.FrameSignalOutcomeMatrix
        Cue.RHI.D3D12.FrameCommand.StateOrderBegin
        Cue.RHI.D3D12.FrameCommand.StateOrderClose
        Cue.RHI.D3D12.FrameCommand.StateOrderExecute
        Cue.RHI.D3D12.FrameCommand.AllocatorResetFailure
        Cue.RHI.D3D12.FrameCommand.CommandListResetFailure
        Cue.RHI.D3D12.FrameCommand.CommandListCloseFailure
        Cue.RHI.D3D12.QueueState.Smoke
        Cue.RHI.D3D12.QueueState.Exhaustion
        Cue.RHI.D3D12.QueueState.WaitRecoveryMatrix
        Cue.RHI.D3D12.QueueState.WaitSentinelRefreshRemoval
        Cue.RHI.D3D12.QueueState.StaleEventFollowup
        Cue.RHI.D3D12.QueueState.WaitFailedUnavailable
        Cue.RHI.D3D12.QueueState.EventCloseFailure
        Cue.RHI.D3D12.QueueState.EventCreateFailure
        Cue.RHI.D3D12.QueueState.SignalCompletion
        Cue.RHI.D3D12.QueueState.SignalDeviceRemoved
        Cue.RHI.D3D12.QueueState.SignalCompletionDeviceRemovedRace
        Cue.RHI.D3D12.QueueState.SignalUnavailable
        Cue.RHI.D3D12.QueueState.SignalEventCloseFailure
        Cue.RHI.D3D12.QueueState.SignalEventCreateFailure
        Cue.RHI.D3D12.QueueState.TerminalSignalCompletion
        Cue.RHI.D3D12.QueueState.TerminalSignalDeviceRemoved
        Cue.RHI.D3D12.QueueState.TerminalSignalUnavailable
        Cue.RHI.D3D12.QueueState.TerminalCloseFailure
        Cue.RHI.Dependencies
    )
endif()

foreach(testTarget IN LISTS cueRhiTestTargets)
    set_target_properties(
        ${testTarget}
        PROPERTIES
            CXX_EXTENSIONS OFF
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
    )

    if(MSVC)
        target_compile_options(
            ${testTarget}
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
            ${testTarget}
            PRIVATE
                $<$<CONFIG:Development>:/DEBUG>
        )
    endif()
endforeach()

cue_verify_rhi_target_shape(Cue.RHI "${PROJECT_SOURCE_DIR}/Engine/Source/RHI/Public")

if(WIN32)
    cue_verify_rhi_target_shape(
        Cue.RHI.D3D12
        "${PROJECT_SOURCE_DIR}/Engine/Source/RHI/D3D12/Public"
    )

    cue_get_rhi_target_property(Cue.RHI LINK_LIBRARIES cueRhiLinks)
    cue_get_rhi_target_property(Cue.RHI INTERFACE_LINK_LIBRARIES cueRhiInterfaceLinks)
    cue_get_rhi_target_property(Cue.RHI MANUALLY_ADDED_DEPENDENCIES cueRhiManualDependencies)
    cue_get_rhi_target_property(Cue.RHI INTERFACE_SOURCES cueRhiInterfaceSources)
    cue_get_rhi_target_property(
        Cue.RHI
        INTERFACE_LINK_LIBRARIES_DIRECT
        cueRhiInterfaceDirectLinks
    )
    cue_get_rhi_target_property(
        Cue.RHI
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        cueRhiInterfaceSystemIncludes
    )
    cue_get_rhi_target_property(Cue.RHI LINK_OPTIONS cueRhiLinkOptions)
    cue_get_rhi_target_property(Cue.RHI INTERFACE_LINK_OPTIONS cueRhiInterfaceLinkOptions)
    cue_get_rhi_target_property(Cue.RHI LINK_FLAGS cueRhiLinkFlags)
    cue_get_rhi_target_property(Cue.RHI STATIC_LIBRARY_OPTIONS cueRhiStaticLibraryOptions)
    cue_get_rhi_target_property(Cue.RHI.D3D12 LINK_LIBRARIES cueRhiD3d12Links)
    cue_get_rhi_target_property(
        Cue.RHI.D3D12
        INTERFACE_LINK_LIBRARIES
        cueRhiD3d12InterfaceLinks
    )
    cue_get_rhi_target_property(
        Cue.RHI.D3D12
        MANUALLY_ADDED_DEPENDENCIES
        cueRhiD3d12ManualDependencies
    )
    cue_get_rhi_target_property(
        Cue.RHI.D3D12
        INTERFACE_SOURCES
        cueRhiD3d12InterfaceSources
    )
    cue_get_rhi_target_property(
        Cue.RHI.D3D12
        INTERFACE_LINK_LIBRARIES_DIRECT
        cueRhiD3d12InterfaceDirectLinks
    )
    cue_get_rhi_target_property(
        Cue.RHI.D3D12
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
        cueRhiD3d12InterfaceSystemIncludes
    )
    cue_get_rhi_target_property(Cue.RHI.D3D12 LINK_OPTIONS cueRhiD3d12LinkOptions)
    cue_get_rhi_target_property(
        Cue.RHI.D3D12
        INTERFACE_LINK_OPTIONS
        cueRhiD3d12InterfaceLinkOptions
    )
    cue_get_rhi_target_property(Cue.RHI.D3D12 LINK_FLAGS cueRhiD3d12LinkFlags)
    cue_get_rhi_target_property(
        Cue.RHI.D3D12
        STATIC_LIBRARY_OPTIONS
        cueRhiD3d12StaticLibraryOptions
    )
    cue_get_rhi_target_property(
        Cue.RHI.D3D12
        CUE_MINIMUM_WINDOWS_SDK_VERSION
        cueRhiD3d12MinimumWindowsSdkVersion
    )

    cue_collect_rhi_target_files(Cue.RHI cueRhiTargetFiles)
    cue_collect_rhi_target_files(Cue.RHI.D3D12 cueRhiD3d12TargetFiles)
    set(allRhiTargetFiles ${cueRhiTargetFiles} ${cueRhiD3d12TargetFiles})
    list(REMOVE_DUPLICATES allRhiTargetFiles)
    string(REPLACE ";" "\n" rhiTargetFileContents "${allRhiTargetFiles}")
    set(rhiTargetFileList "${CMAKE_CURRENT_BINARY_DIR}/Cue.RHI.TargetFiles.txt")
    file(GENERATE OUTPUT "${rhiTargetFileList}" CONTENT "${rhiTargetFileContents}\n")

    set(cueRhiDependencyReport "${CMAKE_CURRENT_BINARY_DIR}/Cue.RHI.Dependencies.txt")

    configure_file(
        RHIDependencyReport.txt.in
        "${cueRhiDependencyReport}"
        @ONLY
    )
endif()

add_custom_target(Cue.RHI.Tests DEPENDS ${cueRhiTestTargets})

add_test(NAME Cue.RHI.PublicHeaders COMMAND CueRhiPublicHeaderTest)

if(WIN32)
    add_test(NAME Cue.RHI.D3D12.PublicHeaders COMMAND CueRhiD3d12PublicHeaderTest)
    add_test(
        NAME Cue.RHI.D3D12.AdapterSelection.Hardware
        COMMAND CueRhiD3d12AdapterSelectionProcess Hardware
    )
    add_test(
        NAME Cue.RHI.D3D12.AdapterSelection.Warp
        COMMAND CueRhiD3d12AdapterSelectionProcess Warp
    )
    add_test(
        NAME Cue.RHI.D3D12.AdapterSelection.Skip
        COMMAND CueRhiD3d12AdapterSelectionProcess Skip
    )
    add_test(
        NAME Cue.RHI.D3D12.AdapterSelection.Mapping
        COMMAND CueRhiD3d12AdapterSelectionProcess Mapping
    )
    add_test(
        NAME Cue.RHI.D3D12.AdapterSelection.FactoryFallback
        COMMAND CueRhiD3d12AdapterSelectionProcess FactoryFallback
    )
    add_test(
        NAME Cue.RHI.D3D12.AdapterSelection.FactoryFallbackLogFailure
        COMMAND CueRhiD3d12AdapterSelectionProcess FactoryFallbackLogFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.AdapterSelection.LogFailure
        COMMAND CueRhiD3d12AdapterSelectionProcess LogFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.Backend.Warp
        COMMAND CueRhiD3d12BackendProcess Warp
    )
    add_test(
        NAME Cue.RHI.D3D12.Backend.DeviceFailure
        COMMAND CueRhiD3d12BackendProcess DeviceFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.Backend.ThreadDestruction
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueRhiD3d12BackendProcess>
            -DEXPECTED_EXIT_CODE=$<IF:$<CONFIG:Release>,0,90>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyD3d12BackendThreadDestruction.cmake
    )
    add_test(
        NAME Cue.RHI.D3D12.Diagnostics.Disabled
        COMMAND CueRhiD3d12DiagnosticsProcess Disabled
    )
    add_test(
        NAME Cue.RHI.D3D12.Diagnostics.Configured
        COMMAND CueRhiD3d12DiagnosticsProcess Configured
    )
    add_test(
        NAME Cue.RHI.D3D12.Backend.InvalidWaitTimeout
        COMMAND CueRhiD3d12BackendProcess InvalidWaitTimeout
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.Smoke300
        COMMAND CueRhiD3d12FrameCommandProcess Smoke300
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.InfoQueue300
        COMMAND CueRhiD3d12FrameCommandProcess InfoQueue300
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.InvalidIndex
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueRhiD3d12FrameCommandProcess>
            -DTEST_MODE=InvalidIndex
            -DEXPECTED_EXIT_CODE=$<IF:$<CONFIG:Release>,0,90>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyD3d12FrameCommandStateOrder.cmake
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.StateOrderBegin
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueRhiD3d12FrameCommandProcess>
            -DTEST_MODE=StateOrderBegin
            -DEXPECTED_EXIT_CODE=$<IF:$<CONFIG:Release>,0,90>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyD3d12FrameCommandStateOrder.cmake
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.WaitRecoveryMatrix
        COMMAND CueRhiD3d12FrameCommandProcess WaitRecoveryMatrix
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.BeginFrameTerminalOutcomeMatrix
        COMMAND CueRhiD3d12FrameCommandProcess BeginFrameTerminalOutcomeMatrix
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.FrameResetFailedTerminalMatrix
        COMMAND CueRhiD3d12FrameCommandProcess FrameResetFailedTerminalMatrix
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.RecordingCloseFailedTerminalMatrix
        COMMAND CueRhiD3d12FrameCommandProcess RecordingCloseFailedTerminalMatrix
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.FenceExhaustionMatrix
        COMMAND CueRhiD3d12FrameCommandProcess FenceExhaustionMatrix
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.FrameSignalOutcomeMatrix
        COMMAND CueRhiD3d12FrameCommandProcess FrameSignalOutcomeMatrix
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.StateOrderClose
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueRhiD3d12FrameCommandProcess>
            -DTEST_MODE=StateOrderClose
            -DEXPECTED_EXIT_CODE=$<IF:$<CONFIG:Release>,0,90>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyD3d12FrameCommandStateOrder.cmake
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.StateOrderExecute
        COMMAND
            ${CMAKE_COMMAND}
            -DTEST_EXECUTABLE=$<TARGET_FILE:CueRhiD3d12FrameCommandProcess>
            -DTEST_MODE=StateOrderExecute
            -DEXPECTED_EXIT_CODE=$<IF:$<CONFIG:Release>,0,90>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyD3d12FrameCommandStateOrder.cmake
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.AllocatorResetFailure
        COMMAND CueRhiD3d12FrameCommandProcess AllocatorResetFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.CommandListResetFailure
        COMMAND CueRhiD3d12FrameCommandProcess CommandListResetFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.FrameCommand.CommandListCloseFailure
        COMMAND CueRhiD3d12FrameCommandProcess CommandListCloseFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.Smoke
        COMMAND CueRhiD3d12QueueStateProcess Smoke
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.Exhaustion
        COMMAND CueRhiD3d12QueueStateProcess Exhaustion
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.WaitRecoveryMatrix
        COMMAND CueRhiD3d12QueueStateProcess WaitRecoveryMatrix
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.WaitSentinelRefreshRemoval
        COMMAND CueRhiD3d12QueueStateProcess WaitSentinelRefreshRemoval
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.WaitFailedUnavailable
        COMMAND CueRhiD3d12QueueStateProcess WaitFailedUnavailable
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.StaleEventFollowup
        COMMAND CueRhiD3d12QueueStateProcess StaleEventFollowup
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.EventCloseFailure
        COMMAND CueRhiD3d12QueueStateProcess EventCloseFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.EventCreateFailure
        COMMAND CueRhiD3d12QueueStateProcess EventCreateFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.SignalCompletion
        COMMAND CueRhiD3d12QueueStateProcess SignalCompletion
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.SignalDeviceRemoved
        COMMAND CueRhiD3d12QueueStateProcess SignalDeviceRemoved
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.SignalCompletionDeviceRemovedRace
        COMMAND CueRhiD3d12QueueStateProcess SignalCompletionDeviceRemovedRace
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.SignalUnavailable
        COMMAND CueRhiD3d12QueueStateProcess SignalUnavailable
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.SignalEventCloseFailure
        COMMAND CueRhiD3d12QueueStateProcess SignalEventCloseFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.SignalEventCreateFailure
        COMMAND CueRhiD3d12QueueStateProcess SignalEventCreateFailure
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.TerminalSignalCompletion
        COMMAND CueRhiD3d12QueueStateProcess TerminalSignalCompletion
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.TerminalSignalDeviceRemoved
        COMMAND CueRhiD3d12QueueStateProcess TerminalSignalDeviceRemoved
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.TerminalSignalUnavailable
        COMMAND CueRhiD3d12QueueStateProcess TerminalSignalUnavailable
    )
    add_test(
        NAME Cue.RHI.D3D12.QueueState.TerminalCloseFailure
        COMMAND CueRhiD3d12QueueStateProcess TerminalCloseFailure
    )
    add_test(
        NAME Cue.RHI.Dependencies
        COMMAND
            ${CMAKE_COMMAND}
            -DREPORT_FILE=${cueRhiDependencyReport}
            -DRHI_SOURCE_DIR=${PROJECT_SOURCE_DIR}/Engine/Source/RHI
            -DRHI_TARGET_FILE_LIST=${rhiTargetFileList}
            -DWINDOWS_SDK_VERSION=${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/VerifyRHIDependencies.cmake
    )
endif()

if(WIN32)
    set_tests_properties(
        Cue.RHI.D3D12.AdapterSelection.Hardware
        Cue.RHI.D3D12.FrameCommand.InfoQueue300
        PROPERTIES
            SKIP_RETURN_CODE 77
    )
endif()

set_tests_properties(
    ${cueRhiTests}
    PROPERTIES
        LABELS RHI
        TIMEOUT 10
)

if(WIN32)
    set_tests_properties(
        Cue.RHI.D3D12.FrameCommand.Smoke300
        Cue.RHI.D3D12.FrameCommand.InfoQueue300
        PROPERTIES
            TIMEOUT 30
    )
endif()
