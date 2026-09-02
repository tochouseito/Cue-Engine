# Benchmark Binaryへ記録するCommitと測定対象Sourceの一致をBuild直前に保証する
if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED REBUILD_ROOT OR NOT DEFINED EXPECTED_COMMIT)
    message(FATAL_ERROR "Benchmark checkout verification requires Git, root, and expected commit")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${REBUILD_ROOT}" rev-parse --show-toplevel
    OUTPUT_VARIABLE repositoryRoot
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE repositoryRootResult
)

if(NOT repositoryRootResult EQUAL 0)
    message(FATAL_ERROR "Failed to resolve the CueEngine Rebuild repository root")
endif()

cmake_path(CONVERT "${repositoryRoot}" TO_CMAKE_PATH_LIST repositoryRoot NORMALIZE)
cmake_path(CONVERT "${REBUILD_ROOT}" TO_CMAKE_PATH_LIST rebuildRoot NORMALIZE)

if(NOT repositoryRoot STREQUAL rebuildRoot)
    message(FATAL_ERROR "REBUILD_ROOT must match the Git repository root: ${repositoryRoot}")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${rebuildRoot}" rev-parse HEAD
    OUTPUT_VARIABLE currentCommit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE currentCommitResult
)

if(NOT currentCommitResult EQUAL 0 OR NOT currentCommit STREQUAL EXPECTED_COMMIT)
    message(FATAL_ERROR "Reconfigure before building the benchmark: expected ${EXPECTED_COMMIT}, current ${currentCommit}")
endif()

execute_process(
    COMMAND
        "${GIT_EXECUTABLE}"
        -C "${rebuildRoot}"
        status
        --porcelain
        --untracked-files=all
        --
        CMakeLists.txt
        Tools/GameCoreBenchmark
        Engine/Source/Foundation
        Engine/Source/Math
        Engine/Source/Schema
        Engine/Source/GameCore
    OUTPUT_VARIABLE rebuildStatus
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE rebuildStatusResult
)

if(NOT rebuildStatusResult EQUAL 0)
    message(FATAL_ERROR "Failed to inspect the Rebuild ECS benchmark inputs")
endif()

if(NOT rebuildStatus STREQUAL "")
    message(FATAL_ERROR "Commit benchmark input changes before building:\n${rebuildStatus}")
endif()
