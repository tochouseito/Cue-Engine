if(NOT DEFINED TEST_EXECUTABLE)
    message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()

if(NOT DEFINED EXPECTED_EXIT_CODE)
    message(FATAL_ERROR "EXPECTED_EXIT_CODE is required")
endif()

if(NOT DEFINED TEST_MODE)
    message(FATAL_ERROR "TEST_MODE is required")
endif()

execute_process(
    COMMAND "${TEST_EXECUTABLE}" "${TEST_MODE}"
    RESULT_VARIABLE testResult
)

if(NOT testResult EQUAL EXPECTED_EXIT_CODE)
    message(
        FATAL_ERROR
        "D3D12 Frame Command state order test ${TEST_MODE} exited with ${testResult}; expected ${EXPECTED_EXIT_CODE}"
    )
endif()
