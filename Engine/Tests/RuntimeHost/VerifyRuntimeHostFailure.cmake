cmake_minimum_required(VERSION 4.2.0)

if(NOT DEFINED TEST_EXECUTABLE)
    message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()

execute_process(
    COMMAND
        "${TEST_EXECUTABLE}"
        --smoke-test
        --width 2147483648
        --height 360
    RESULT_VARIABLE testResult
    OUTPUT_VARIABLE testOutput
    ERROR_VARIABLE testError
)

if(NOT testResult EQUAL 2)
    message(
        FATAL_ERROR
        "Runtime Host initialization failure exited with ${testResult}; expected 2\n${testOutput}\n${testError}"
    )
endif()

set(combinedOutput "${testOutput}\n${testError}")
string(FIND "${combinedOutput}" "Runtime Host failed to create Window" messagePosition)

if(messagePosition EQUAL -1)
    message(FATAL_ERROR "Runtime Host initialization failure did not emit its diagnostic")
endif()

message(STATUS "CueRuntimeHost initialization failure cleanup: passed")
