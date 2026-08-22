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
    COMMAND "${TEST_EXECUTABLE}" --graphics-smoke "${MODE}"
    RESULT_VARIABLE testResult
    OUTPUT_VARIABLE testOutput
    ERROR_VARIABLE testError
)

if(NOT testResult EQUAL 0)
    message(
        FATAL_ERROR
        "D3D12 Device smoke exited with ${testResult}\n${testOutput}\n${testError}"
    )
endif()

set(combinedOutput "${testOutput}\n${testError}")

foreach(
    requiredMessage
    IN ITEMS
        "D3D12 Device Smoke ready"
        "D3D12 Device Smoke shutdown completed"
)
    string(FIND "${combinedOutput}" "${requiredMessage}" messagePosition)

    if(messagePosition EQUAL -1)
        message(FATAL_ERROR "D3D12 Device smoke output is missing: ${requiredMessage}")
    endif()
endforeach()

string(
    FIND
    "${combinedOutput}"
    "[Error] D3D12診断メッセージを取得しました"
    liveObjectErrorPosition
)

if(NOT liveObjectErrorPosition EQUAL -1)
    message(FATAL_ERROR "D3D12 Device smoke emitted a D3D12 Error\n${combinedOutput}")
endif()

message(STATUS "D3D12 Device smoke (${MODE}): passed")
