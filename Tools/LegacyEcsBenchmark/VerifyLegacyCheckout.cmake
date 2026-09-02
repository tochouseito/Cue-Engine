# Configure後に旧Checkoutが変化したBaselineを拒否する
foreach(requiredVariable GIT_EXECUTABLE LEGACY_ROOT EXPECTED_COMMIT)
    if(NOT DEFINED ${requiredVariable} OR "${${requiredVariable}}" STREQUAL "")
        message(FATAL_ERROR "${requiredVariable} is required")
    endif()
endforeach()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${LEGACY_ROOT}" status --porcelain
    OUTPUT_VARIABLE legacyStatus
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE legacyStatusResult
)

if(NOT legacyStatusResult EQUAL 0 OR NOT legacyStatus STREQUAL "")
    message(FATAL_ERROR "The legacy ECS checkout changed after Configure")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${LEGACY_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE currentCommit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE currentCommitResult
)

if(NOT currentCommitResult EQUAL 0 OR NOT currentCommit STREQUAL EXPECTED_COMMIT)
    message(FATAL_ERROR "The legacy ECS commit changed after Configure: expected ${EXPECTED_COMMIT}, found ${currentCommit}")
endif()
