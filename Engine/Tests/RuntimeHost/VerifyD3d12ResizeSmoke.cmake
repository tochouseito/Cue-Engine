cmake_minimum_required(VERSION 4.2.0)

if(NOT DEFINED TEST_EXECUTABLE)
    message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()

if(NOT DEFINED MODE)
    message(FATAL_ERROR "MODE is required")
endif()

if(MODE STREQUAL "hardware")
    if(NOT DEFINED ADAPTER_PROBE_EXECUTABLE)
        message(FATAL_ERROR "ADAPTER_PROBE_EXECUTABLE is required for hardware mode")
    endif()

    execute_process(
        COMMAND "${ADAPTER_PROBE_EXECUTABLE}" Hardware
        RESULT_VARIABLE adapterProbeResult
        OUTPUT_VARIABLE adapterProbeOutput
        ERROR_VARIABLE adapterProbeError
    )

    if(adapterProbeResult EQUAL 77)
        cmake_language(EXIT 77)
    endif()

    if(NOT adapterProbeResult EQUAL 0)
        message(
            FATAL_ERROR
            "Independent hardware Adapter probe failed with ${adapterProbeResult}\n${adapterProbeOutput}\n${adapterProbeError}"
        )
    endif()
endif()

execute_process(
    COMMAND "${TEST_EXECUTABLE}" --resize-smoke "${MODE}" --width 640 --height 360
    RESULT_VARIABLE testResult
    OUTPUT_VARIABLE testOutput
    ERROR_VARIABLE testError
)

if(NOT testResult EQUAL 0)
    message(FATAL_ERROR "D3D12 Resize smoke exited with ${testResult}\n${testOutput}\n${testError}")
endif()

set(combinedOutput "${testOutput}\n${testError}")

foreach(
    requiredMessage
    IN ITEMS
        "D3D12 Render Loop ready: Width=640, Height=360, BufferCount=2, VSync=true"
        "D3D12 Resize Smoke completed: ResizeEventCount=102, MinimizeEventCount=50, RestoreEventCount=50, ResizeApplyCount=150, MinimizedFrameSkipCount=50, PresentedFrameCount=100"
        "D3D12 Render Loop completed: FrameCount=101"
        "D3D12 Render Loop shutdown completed"
)
    string(FIND "${combinedOutput}" "${requiredMessage}" messagePosition)

    if(messagePosition EQUAL -1)
        message(FATAL_ERROR "D3D12 Resize smoke output is missing: ${requiredMessage}\n${combinedOutput}")
    endif()
endforeach()

string(FIND "${combinedOutput}" "[Error] D3D12診断メッセージを取得しました" liveObjectErrorPosition)

if(NOT liveObjectErrorPosition EQUAL -1)
    message(FATAL_ERROR "D3D12 Resize smoke emitted a D3D12 Error\n${combinedOutput}")
endif()

message(STATUS "D3D12 Resize smoke (${MODE}): passed")
